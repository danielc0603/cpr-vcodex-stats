#include "ReadingStatsDetailActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "BookReadingAdjustmentActivity.h"
#include "RecentBooksStore.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/RecentBooksGrid.h"
#include "util/TimeUtils.h"

namespace {
constexpr int COVER_WIDTH = 132;
constexpr int COVER_HEIGHT = 198;
constexpr int DONUT_RADIUS = 28;
constexpr int DONUT_THICKNESS = 6;
constexpr int METRIC_ROW_HEIGHT = 27;
constexpr int TOP_CARD_HEIGHT = 226;
constexpr int SUMMARY_BANNER_HEIGHT = 46;
constexpr int SUMMARY_BANNER_GAP = 8;
constexpr int DETAIL_SCROLL_STEP = 110;
constexpr uint32_t SCROLL_REPEAT_START_MS = 260;
constexpr uint32_t SCROLL_REPEAT_INTERVAL_MS = 130;
constexpr uint32_t HOLD_PREVIEW_MS = 250;
constexpr uint32_t ADJUST_READING_TIME_LONG_PRESS_MS = 1100;
constexpr size_t MAX_RESOLVED_COVERS = 16;
constexpr int COVER_ANALYSIS_MAX_SAMPLES = 1200;

struct ResolvedCoverCacheEntry {
  std::string bookPath;
  std::string coverBmpPath;
  std::string resolvedPath;
};

struct CoverToneAnalysis {
  int dark = 0;
  int mid = 0;
  int light = 0;
  int samples = 0;

  bool shouldInvert() const {
    if (samples < 32) {
      return false;
    }

    const int darkish = dark + mid;
    return darkish * 100 / samples >= 78 && light * 100 / samples <= 18;
  }
};

std::vector<ResolvedCoverCacheEntry>& getResolvedCoverCache() {
  static std::vector<ResolvedCoverCacheEntry> cache;
  return cache;
}

std::string getCachedResolvedCoverPath(const ReadingBookStats& book) {
  auto& cache = getResolvedCoverCache();
  for (auto it = cache.begin(); it != cache.end(); ++it) {
    if (it->bookPath != book.path || it->coverBmpPath != book.coverBmpPath) {
      continue;
    }
    if (!it->resolvedPath.empty() && Storage.exists(it->resolvedPath.c_str())) {
      if (it != cache.begin()) {
        ResolvedCoverCacheEntry entry = *it;
        cache.erase(it);
        cache.insert(cache.begin(), std::move(entry));
      }
      return cache.front().resolvedPath;
    }
    break;
  }
  return "";
}

void rememberResolvedCoverPath(const ReadingBookStats& book, const std::string& resolvedPath) {
  if (resolvedPath.empty()) {
    return;
  }

  auto& cache = getResolvedCoverCache();
  cache.erase(std::remove_if(cache.begin(), cache.end(),
                             [&](const ResolvedCoverCacheEntry& entry) { return entry.bookPath == book.path; }),
              cache.end());
  cache.insert(cache.begin(), ResolvedCoverCacheEntry{book.path, book.coverBmpPath, resolvedPath});
  if (cache.size() > MAX_RESOLVED_COVERS) {
    cache.pop_back();
  }
}

ReadingBookStats withCoverPath(const ReadingBookStats& book, const std::string& coverBmpPath) {
  ReadingBookStats updated = book;
  updated.coverBmpPath = coverBmpPath;
  return updated;
}

const ReadingBookStats* findBook(const std::string& bookPath) {
  for (const auto& book : READING_STATS.getBooks()) {
    if (book.path == bookPath) {
      return &book;
    }
  }
  return nullptr;
}

std::string resolveStoredCoverPath(const std::string& bookPath, const std::string& coverBmpPath) {
  RecentBook book;
  book.path = bookPath;
  book.coverBmpPath = coverBmpPath;
  return RecentBooksGrid::resolveExistingCoverPath(book);
}

std::string findRecentBookCoverPath(const ReadingBookStats& book) {
  for (const RecentBook& recentBook : RECENT_BOOKS.getBooks()) {
    const bool samePath = recentBook.path == book.path;
    const bool sameMetadata = !book.title.empty() && recentBook.title == book.title &&
                              (book.author.empty() || recentBook.author == book.author);
    if (!samePath && !sameMetadata) {
      continue;
    }

    const std::string resolved = resolveStoredCoverPath(recentBook.path, recentBook.coverBmpPath);
    if (!resolved.empty()) {
      READING_STATS.updateBookMetadata(book.path, recentBook.title, recentBook.author, recentBook.coverBmpPath);
      rememberResolvedCoverPath(withCoverPath(book, recentBook.coverBmpPath), resolved);
      return resolved;
    }
  }
  return "";
}

std::string ensureCoverPath(GfxRenderer& renderer, const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  const std::string recentResolved = findRecentBookCoverPath(book);
  if (!recentResolved.empty()) {
    return recentResolved;
  }

  RecentBook gridBook{book.bookId, book.path, book.title, book.author, book.coverBmpPath};
  std::string resolved = RecentBooksGrid::loadSingleCover(renderer, gridBook);
  if (!resolved.empty()) {
    READING_STATS.updateBookMetadata(book.path, gridBook.title, gridBook.author, gridBook.coverBmpPath);
    rememberResolvedCoverPath(withCoverPath(book, gridBook.coverBmpPath), resolved);
    return resolved;
  }

  return "";
}

std::string findFastCoverPath(const ReadingBookStats& book) {
  const std::string cachedResolvedPath = getCachedResolvedCoverPath(book);
  if (!cachedResolvedPath.empty()) {
    return cachedResolvedPath;
  }

  std::string resolved = resolveStoredCoverPath(book.path, book.coverBmpPath);
  if (!resolved.empty()) {
    rememberResolvedCoverPath(book, resolved);
    return resolved;
  }

  if (!Storage.exists(book.path.c_str())) {
    return "";
  }

  resolved = findRecentBookCoverPath(book);
  if (!resolved.empty()) {
    return resolved;
  }

  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(book.path, epub.getCoverBmpPath());
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(book.path, xtc.getCoverBmpPath());
  } else if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    Txt txt(book.path, "/.crosspoint");
    resolved = resolveStoredCoverPath(book.path, txt.getCoverBmpPath());
  }

  if (!resolved.empty()) {
    READING_STATS.updateBookMetadata(book.path, "", "", resolved);
    rememberResolvedCoverPath(withCoverPath(book, resolved), resolved);
  }
  return resolved;
}

std::string getDisplayTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string formatDate(const uint32_t timestamp) {
  const std::string formatted = TimeUtils::formatDate(timestamp);
  return formatted.empty() ? std::string(tr(STR_NOT_SET)) : formatted;
}

std::string formatDateRange(const uint32_t startTimestamp, const uint32_t endTimestamp) {
  const std::string start = TimeUtils::formatDate(startTimestamp);
  const std::string end = TimeUtils::formatDate(endTimestamp);
  return (start.empty() ? "?" : start) + " - " + (end.empty() ? "?" : end);
}

std::string trimCopy(const std::string& value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
  if (first >= last) {
    return "";
  }
  return std::string(first, last);
}

bool startsWithChapterWord(const std::string& value) {
  constexpr char chapter[] = "chapter";
  if (value.size() < sizeof(chapter) - 1) {
    return false;
  }
  for (size_t index = 0; index < sizeof(chapter) - 1; ++index) {
    if (std::tolower(static_cast<unsigned char>(value[index])) != chapter[index]) {
      return false;
    }
  }
  return true;
}

std::string formatChapterRowLabel(const ReadingBookStats& book) {
  const std::string chapter = trimCopy(book.chapterTitle);
  const std::string prefix = std::string(tr(STR_CHAPTER));
  if (chapter.empty()) {
    return prefix + ": " + tr(STR_UNKNOWN);
  }
  if (startsWithChapterWord(chapter)) {
    return chapter;
  }
  if (std::isdigit(static_cast<unsigned char>(chapter.front()))) {
    return prefix + " " + chapter;
  }
  return prefix + ": " + chapter;
}

uint32_t getCompletionDateForDisplay(const ReadingBookStats& book) { return book.completedAt; }

bool pointInProgressArc(const int dx, const int dy, const float progress) {
  constexpr float kPi = 3.1415926535f;
  if (progress >= 0.999f) {
    return true;
  }
  float angle = atan2f(static_cast<float>(dy), static_cast<float>(dx)) + kPi / 2.0f;
  while (angle < 0.0f) {
    angle += kPi * 2.0f;
  }
  const float normalized = angle / (kPi * 2.0f);
  return normalized <= progress;
}

void drawDonutGauge(GfxRenderer& renderer, const int cx, const int cy, const int radius, const int thickness,
                    const uint8_t percent, const char* label = "") {
  const int innerRadius = std::max(2, radius - thickness);
  const int outer2 = radius * radius;
  const int inner2 = innerRadius * innerRadius;
  const float progress = std::min<int>(percent, 100) / 100.0f;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      const int d2 = x * x + y * y;
      if (d2 <= inner2 || d2 > outer2) {
        continue;
      }
      if (pointInProgressArc(x, y, progress)) {
        renderer.drawPixel(cx + x, cy + y, true);
      } else if (((x + y) & 3) == 0) {
        renderer.drawPixel(cx + x, cy + y, true);
      }
    }
  }
  const std::string percentText = std::to_string(std::min<int>(percent, 100)) + "%";
  const int percentWidth = renderer.getTextWidth(UI_10_FONT_ID, percentText.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, cx - percentWidth / 2, cy - 7, percentText.c_str(), true, EpdFontFamily::BOLD);
  if (label != nullptr && label[0] != '\0') {
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, cx - labelWidth / 2, cy + radius + 7, label);
  }
}

Rect offsetRect(Rect rect, const int dy) {
  rect.y += dy;
  return rect;
}

void drawSummaryBanner(GfxRenderer& renderer, const Rect& rect, const char* title, const std::string& summary,
                       const bool inverted = false) {
  if (inverted) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 6, Color::Black);
  } else {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }

  renderer.drawText(UI_10_FONT_ID, rect.x + 10, rect.y + 6, title, !inverted, EpdFontFamily::BOLD);
  const auto summaryLines =
      renderer.wrappedText(UI_10_FONT_ID, summary.c_str(), rect.width - 20, 2, EpdFontFamily::REGULAR);
  int summaryY = rect.y + 23;
  for (const auto& line : summaryLines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + 10, summaryY, line.c_str(), !inverted, EpdFontFamily::REGULAR);
    summaryY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

void drawStatsTableRow(GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                       const bool selected = false) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);

  const int pad = 4;
  const int valueWidth = std::max(72, rect.width / 2);
  const int labelWidth = std::max(40, rect.width - valueWidth - pad * 3);
  const std::string safeLabel = renderer.truncatedText(SMALL_FONT_ID, label, labelWidth, EpdFontFamily::BOLD);
  const std::string safeValue = renderer.truncatedText(UI_10_FONT_ID, value.c_str(), valueWidth, EpdFontFamily::BOLD);
  const int valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, safeValue.c_str(), EpdFontFamily::BOLD);
  const int y = rect.y + std::max(2, (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
  renderer.drawText(SMALL_FONT_ID, rect.x + pad, y + 1, safeLabel.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - pad - valueTextWidth, y, safeValue.c_str(), true,
                    EpdFontFamily::BOLD);
}

void drawProgressBlock(GfxRenderer& renderer, const Rect& rect, const char* label, const uint8_t percent) {
  const std::string percentText = std::to_string(std::min<int>(percent, 100)) + "%";
  const int percentWidth = renderer.getTextWidth(UI_10_FONT_ID, percentText.c_str(), EpdFontFamily::BOLD);

  renderer.drawText(UI_10_FONT_ID, rect.x, rect.y, label);
  renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - percentWidth, rect.y, percentText.c_str(), true,
                    EpdFontFamily::BOLD);

  const Rect barRect{rect.x, rect.y + 23, rect.width, 10};
  renderer.drawRect(barRect.x, barRect.y, barRect.width, barRect.height);
  const int fillWidth = std::max(0, barRect.width - 4) * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) {
    renderer.fillRect(barRect.x + 2, barRect.y + 2, fillWidth, std::max(0, barRect.height - 4));
  }
}

CoverToneAnalysis analyzeCoverTone(const Bitmap& bitmap) {
  CoverToneAnalysis analysis;
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes || bitmap.rewindToData() != BmpReaderError::Ok) {
    free(outputRow);
    free(rowBytes);
    return analysis;
  }

  const int targetRowSamples = std::max(1, COVER_ANALYSIS_MAX_SAMPLES / std::max(1, bitmap.getHeight()));
  const int xStep = std::max(1, bitmap.getWidth() / targetRowSamples);
  for (int y = 0; y < bitmap.getHeight(); ++y) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      break;
    }
    for (int x = 0; x < bitmap.getWidth(); x += xStep) {
      const uint8_t val = (outputRow[x / 4] >> (6 - ((x * 2) % 8))) & 0x03;
      if (val == 0) {
        ++analysis.dark;
      } else if (val == 3) {
        ++analysis.light;
      } else {
        ++analysis.mid;
      }
      ++analysis.samples;
    }
  }

  bitmap.rewindToData();
  free(outputRow);
  free(rowBytes);
  return analysis;
}

void drawInvertedBitmapCover(GfxRenderer& renderer, const Bitmap& bitmap, const Rect& frame) {
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes || bitmap.rewindToData() != BmpReaderError::Ok) {
    free(outputRow);
    free(rowBytes);
    return;
  }

  const float scaleX = static_cast<float>(frame.width) / static_cast<float>(std::max(1, bitmap.getWidth()));
  const float scaleY = static_cast<float>(frame.height) / static_cast<float>(std::max(1, bitmap.getHeight()));
  for (int bmpY = 0; bmpY < bitmap.getHeight(); ++bmpY) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      break;
    }

    const int sourceY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    const int screenY = frame.y + static_cast<int>(sourceY * scaleY);
    if (screenY < frame.y || screenY >= frame.y + frame.height) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); ++bmpX) {
      const int screenX = frame.x + static_cast<int>(bmpX * scaleX);
      if (screenX < frame.x || screenX >= frame.x + frame.width) {
        continue;
      }

      const uint8_t sourceVal = (outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8))) & 0x03;
      const uint8_t val = 3 - sourceVal;
      const bool drawBlack = val == 0 || (val == 1 && ((screenX + screenY) & 1) == 0) ||
                             (val == 2 && (screenX & 1) == 0 && (screenY & 1) == 0);
      renderer.drawPixel(screenX, screenY, drawBlack);
    }
  }

  bitmap.rewindToData();
  free(outputRow);
  free(rowBytes);
}

void drawCover(GfxRenderer& renderer, const Rect& rect, const std::string& coverPath) {
  const auto coverFrameForRatio = [](const Rect& bounds, const int sourceW, const int sourceH) {
    const int safeSourceW = sourceW > 0 ? sourceW : 2;
    const int safeSourceH = sourceH > 0 ? sourceH : 3;
    int frameW = std::min(bounds.width - 4, static_cast<int>((static_cast<int64_t>(bounds.height - 4) * safeSourceW) /
                                                             safeSourceH));
    int frameH = static_cast<int>((static_cast<int64_t>(frameW) * safeSourceH) / safeSourceW);
    if (frameH > bounds.height - 4) {
      frameH = bounds.height - 4;
      frameW = static_cast<int>((static_cast<int64_t>(frameH) * safeSourceW) / safeSourceH);
    }
    frameW = std::max(1, std::min(frameW, bounds.width - 4));
    frameH = std::max(1, std::min(frameH, bounds.height - 4));
    return Rect{bounds.x + 2 + std::max(0, (bounds.width - 4 - frameW) / 2),
                bounds.y + 2 + std::max(0, (bounds.height - 4 - frameH) / 2), frameW, frameH};
  };

  const auto drawFallback = [&renderer, &rect]() {
    const char* label = tr(STR_BOOK);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int textY = rect.y + rect.height / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
  };

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  if (coverPath.empty()) {
    drawFallback();
    return;
  }

  FsFile file;
  if (!Storage.openFileForRead("RSD", coverPath, file)) {
    drawFallback();
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    const Rect frame = coverFrameForRatio(rect, std::max(1, bitmap.getWidth()), std::max(1, bitmap.getHeight()));
    const CoverToneAnalysis analysis = analyzeCoverTone(bitmap);
    renderer.fillRect(frame.x, frame.y, frame.width, frame.height, false);
    if (analysis.shouldInvert()) {
      drawInvertedBitmapCover(renderer, bitmap, frame);
    } else {
      renderer.drawBitmap(bitmap, frame.x, frame.y, frame.width, frame.height);
    }
  } else {
    drawFallback();
  }
  file.close();
}

void drawHoldPreview(GfxRenderer& renderer, const std::string& text) {
  if (text.empty()) {
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int horizontalPadding = 12;
  constexpr int verticalPadding = 6;
  constexpr int radius = 6;
  const int maxTextWidth = std::max(24, pageWidth - metrics.contentSidePadding * 2 - horizontalPadding * 2);
  const std::string safeText = renderer.truncatedText(SMALL_FONT_ID, text.c_str(), maxTextWidth,
                                                      EpdFontFamily::BOLD);
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, safeText.c_str(), EpdFontFamily::BOLD);
  const int pillWidth = std::min(std::max(48, pageWidth - metrics.contentSidePadding * 2),
                                 textWidth + horizontalPadding * 2);
  const int pillHeight = lineHeight + verticalPadding * 2;
  const int x = (pageWidth - pillWidth) / 2;
  const int y = std::max(metrics.topPadding,
                         renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing -
                             pillHeight - 10);
  renderer.fillRoundedRect(x, y, pillWidth, pillHeight, radius, Color::Black);
  renderer.drawRoundedRect(x, y, pillWidth, pillHeight, 1, radius, true);
  renderer.drawText(SMALL_FONT_ID, x + (pillWidth - textWidth) / 2, y + verticalPadding, safeText.c_str(), false,
                    EpdFontFamily::BOLD);
}
}  // namespace

void ReadingStatsDetailActivity::onEnter() {
  Activity::onEnter();
  invalidateBaseScreenBuffer();
  resolvedCoverBmpPath.clear();
  coverLoadPending = false;
  scrollOffset = 0;
  maxScrollOffset = 0;
  lastScrollActionMs = 0;
  scrollDirection = 0;
  confirmLongPressHandled = false;
  holdPreviewVisible = false;
  waitForConfirmRelease =
      mappedInput.isPressed(MappedInputManager::Button::Confirm) || mappedInput.isAnyMappedButtonPressed();
  waitForBackRelease = false;
  if (const auto* book = findBook(bookPath)) {
    resolvedCoverBmpPath = findFastCoverPath(*book);
    coverLoadPending = resolvedCoverBmpPath.empty();
  }
  requestUpdate();
}

void ReadingStatsDetailActivity::onExit() {
  Activity::onExit();
  freeBaseScreenBuffer();
}

bool ReadingStatsDetailActivity::storeBaseScreenBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeBaseScreenBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  baseScreenBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!baseScreenBuffer) {
    return false;
  }

  memcpy(baseScreenBuffer, frameBuffer, bufferSize);
  baseScreenBufferStored = true;
  baseScreenBookPath = bookPath;
  baseScreenCoverPath = resolvedCoverBmpPath;
  baseScreenScrollOffset = scrollOffset;
  return true;
}

bool ReadingStatsDetailActivity::restoreBaseScreenBuffer() {
  if (!baseScreenBufferStored || !baseScreenBuffer || baseScreenBookPath != bookPath ||
      baseScreenCoverPath != resolvedCoverBmpPath || baseScreenScrollOffset != scrollOffset) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  memcpy(frameBuffer, baseScreenBuffer, renderer.getBufferSize());
  return true;
}

void ReadingStatsDetailActivity::invalidateBaseScreenBuffer() {
  baseScreenBufferStored = false;
  baseScreenBookPath.clear();
  baseScreenCoverPath.clear();
  baseScreenScrollOffset = -1;
}

void ReadingStatsDetailActivity::freeBaseScreenBuffer() {
  if (baseScreenBuffer) {
    free(baseScreenBuffer);
    baseScreenBuffer = nullptr;
  }
  invalidateBaseScreenBuffer();
}

void ReadingStatsDetailActivity::openAdjustment() {
  const auto* book = findBook(bookPath);
  if (book == nullptr) {
    requestUpdate();
    return;
  }

  startActivityForResult(
      std::make_unique<BookReadingAdjustmentActivity>(renderer, mappedInput, bookPath, getDisplayTitle(*book)),
      [this](const ActivityResult&) {
        guardChildReturn();
        requestUpdate();
      });
}

void ReadingStatsDetailActivity::guardChildReturn() {
  invalidateBaseScreenBuffer();
  mappedInput.armPressedButtonsReleaseGuard();
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  waitForConfirmRelease =
      mappedInput.isPressed(MappedInputManager::Button::Confirm) || mappedInput.isAnyMappedButtonPressed();
  confirmLongPressHandled = false;
}

void ReadingStatsDetailActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) && !mappedInput.isAnyMappedButtonPressed()) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) && !mappedInput.isAnyMappedButtonPressed()) {
      waitForConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmLongPressHandled = false;
    holdPreviewVisible = false;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && !confirmLongPressHandled &&
      mappedInput.getHeldTime() >= HOLD_PREVIEW_MS && mappedInput.getHeldTime() < ADJUST_READING_TIME_LONG_PRESS_MS &&
      !holdPreviewVisible) {
    holdPreviewVisible = true;
    requestUpdate();
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && !confirmLongPressHandled &&
      mappedInput.getHeldTime() >= ADJUST_READING_TIME_LONG_PRESS_MS) {
    confirmLongPressHandled = true;
    holdPreviewVisible = false;
    mappedInput.armPressedButtonsReleaseGuard();
    openAdjustment();
    return;
  }

  const auto scrollBy = [&](const int delta) {
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset != scrollOffset) {
      scrollOffset = nextOffset;
      invalidateBaseScreenBuffer();
      requestUpdate();
    }
  };

  int requestedDirection = 0;
  if (mappedInput.isPressed(MappedInputManager::Button::Up) || mappedInput.isPressed(MappedInputManager::Button::Left)) {
    requestedDirection = -1;
  } else if (mappedInput.isPressed(MappedInputManager::Button::Down) ||
             mappedInput.isPressed(MappedInputManager::Button::Right)) {
    requestedDirection = 1;
  }

  if (requestedDirection != 0 && maxScrollOffset > 0) {
    const bool directionChanged = requestedDirection != scrollDirection;
    const bool wasPressedNow =
        (requestedDirection < 0 && (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Left))) ||
        (requestedDirection > 0 && (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Right)));
    const bool canRepeat = mappedInput.getHeldTime() >= SCROLL_REPEAT_START_MS &&
                           (lastScrollActionMs == 0 || millis() - lastScrollActionMs >= SCROLL_REPEAT_INTERVAL_MS);
    if (directionChanged || wasPressedNow || canRepeat) {
      scrollBy(requestedDirection * DETAIL_SCROLL_STEP);
      lastScrollActionMs = millis();
    }
  } else {
    lastScrollActionMs = 0;
  }
  scrollDirection = requestedDirection;

  if (coverLoadPending) {
    coverLoadPending = false;
    if (const auto* book = findBook(bookPath)) {
      const std::string resolvedCoverPath = ensureCoverPath(renderer, *book);
      if (!resolvedCoverPath.empty() && resolvedCoverPath != resolvedCoverBmpPath) {
        resolvedCoverBmpPath = resolvedCoverPath;
        invalidateBaseScreenBuffer();
        requestUpdate();
      }
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (holdPreviewVisible) {
      holdPreviewVisible = false;
      requestUpdate();
    }
    if (confirmLongPressHandled) {
      confirmLongPressHandled = false;
      return;
    }
    if (!Storage.exists(bookPath.c_str())) {
      return;
    }
    onSelectBook(bookPath);
  }
}

void ReadingStatsDetailActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const auto* book = findBook(bookPath);
  const auto& lastSessionSnapshot = READING_STATS.getLastSessionSnapshot();
  const bool showCompletionBanner = context.showSessionSummary && lastSessionSnapshot.valid &&
                                    lastSessionSnapshot.path == bookPath && lastSessionSnapshot.completedThisSession;

  if (!book) {
    renderer.clearScreen();
    invalidateBaseScreenBuffer();
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 30,
                      tr(STR_NO_READING_STATS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int fullWidth = pageWidth - metrics.contentSidePadding * 2;
  const Rect topCard{metrics.contentSidePadding, contentTop, fullWidth, TOP_CARD_HEIGHT};
  const Rect coverBaseRect{topCard.x + 8, topCard.y + 8, COVER_WIDTH, COVER_HEIGHT};
  const int detailsX = coverBaseRect.x + coverBaseRect.width + 12;
  const int detailsWidth = std::max(64, topCard.x + topCard.width - detailsX - 8);
  const int titleWidth = detailsWidth;
  const int titleX = detailsX;
  const int titleTop = topCard.y + 10;
  const auto wrappedTitle =
      renderer.wrappedText(UI_12_FONT_ID, getDisplayTitle(*book).c_str(), titleWidth, 2, EpdFontFamily::BOLD);
  int currentY = titleTop + static_cast<int>(wrappedTitle.size()) * renderer.getLineHeight(UI_12_FONT_ID);
  const int authorTop = currentY + 4;
  currentY += book->author.empty() ? 8 : renderer.getLineHeight(UI_10_FONT_ID) + 10;

  int cardsTop = topCard.y + topCard.height + metrics.verticalSpacing;
  const int summaryBannerTop = cardsTop;
  if (showCompletionBanner) {
    cardsTop += SUMMARY_BANNER_HEIGHT + SUMMARY_BANNER_GAP;
  }

  constexpr int metricRowCount = 10;
  const int contentBottom = cardsTop + metricRowCount * METRIC_ROW_HEIGHT +
                            metrics.verticalSpacing;
  maxScrollOffset = std::max(0, contentBottom - viewportBottom);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);
  const int scrollDy = -scrollOffset;
  const Rect coverRect = offsetRect(coverBaseRect, scrollDy);

  const bool baseScreenRestored = restoreBaseScreenBuffer();
  if (!baseScreenRestored) {
    renderer.clearScreen();
    drawCover(renderer, coverRect, resolvedCoverBmpPath);

    currentY = titleTop + scrollDy;
    for (const auto& line : wrappedTitle) {
      const int lineW = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, titleX + std::max(0, (titleWidth - lineW) / 2), currentY, line.c_str(), true,
                        EpdFontFamily::BOLD);
      currentY += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (!book->author.empty()) {
      const std::string author =
          renderer.truncatedText(UI_10_FONT_ID, book->author.c_str(), titleWidth, EpdFontFamily::REGULAR);
      const int authorW = renderer.getTextWidth(UI_10_FONT_ID, author.c_str());
      renderer.drawText(UI_10_FONT_ID, titleX + std::max(0, (titleWidth - authorW) / 2), authorTop + scrollDy,
                        author.c_str());
      currentY = authorTop + scrollDy + renderer.getLineHeight(UI_10_FONT_ID) + 8;
    } else {
      currentY += 8;
    }

    const auto bookEstimate = ReadingStatsAnalytics::buildBookTimeLeftEstimate(*book);
    const std::string progressGainValue =
        std::to_string(ReadingStatsAnalytics::getTrackedProgressGainPercent(*book)) + "%";
    const std::string paceTrendValue = ReadingStatsAnalytics::formatPaceTrend(*book);
    const std::string confidenceValue =
        bookEstimate.ready ? ReadingStatsAnalytics::formatEstimateConfidence(bookEstimate.confidence)
                           : ReadingStatsAnalytics::formatEstimateConfidence(
                                 ReadingStatsAnalytics::EstimateConfidence::LOW_CONFIDENCE);

    const int progressTop = currentY + 2;
    drawProgressBlock(renderer, Rect{detailsX, progressTop, detailsWidth, 34}, tr(STR_BOOK),
                      book->lastProgressPercent);
    drawProgressBlock(renderer,
                      Rect{detailsX, progressTop + 46, detailsWidth, 34}, tr(STR_CHAPTER),
                      std::min<uint8_t>(book->chapterProgressPercent, 100));
    const std::string bookTimeLeft = ReadingStatsAnalytics::formatTimeLeftEstimate(bookEstimate);
    const int timeLeftTop = progressTop + 94;
    renderer.drawText(SMALL_FONT_ID, detailsX, timeLeftTop, tr(STR_BOOK_TIME_LEFT), true, EpdFontFamily::BOLD);
    const std::string safeTimeLeft =
        renderer.truncatedText(UI_10_FONT_ID, bookTimeLeft.c_str(), detailsWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, detailsX, timeLeftTop + renderer.getLineHeight(SMALL_FONT_ID) + 4,
                      safeTimeLeft.c_str(), true, EpdFontFamily::BOLD);

    int drawCardsTop = cardsTop + scrollDy;
    if (showCompletionBanner) {
      drawSummaryBanner(
          renderer,
          Rect{metrics.contentSidePadding, summaryBannerTop + scrollDy, pageWidth - metrics.contentSidePadding * 2,
               SUMMARY_BANNER_HEIGHT},
          tr(STR_BOOK_FINISHED), tr(STR_COMPLETED_THIS_SESSION), true);
    }

    const Rect tableRect{metrics.contentSidePadding, drawCardsTop, pageWidth - metrics.contentSidePadding * 2,
                         metricRowCount * METRIC_ROW_HEIGHT};
    int rowIndex = 0;
    const auto drawRow = [&](const std::string& label, const std::string& value) {
      drawStatsTableRow(renderer,
                        Rect{tableRect.x, tableRect.y + rowIndex * METRIC_ROW_HEIGHT, tableRect.width,
                             METRIC_ROW_HEIGHT},
                        label.c_str(), value);
      rowIndex++;
    };
    drawRow(formatChapterRowLabel(*book), std::to_string(std::min<int>(book->chapterProgressPercent, 100)) + "%");
    drawRow(tr(STR_LAST_SESSION), ReadingStatsAnalytics::formatDurationHm(book->lastSessionMs));
    drawRow(tr(STR_TOTAL_SESSIONS),
            std::to_string(book->sessions) + " (" + ReadingStatsAnalytics::formatDurationHm(book->totalReadingMs) + ")");
    drawRow(tr(STR_AVG_PACE),
            ReadingStatsAnalytics::formatProgressPace(ReadingStatsAnalytics::getAverageProgressPaceTenths(*book)));
    drawRow(tr(STR_RECENT_PACE),
            ReadingStatsAnalytics::formatProgressPace(ReadingStatsAnalytics::getRecentProgressPaceTenths(*book)));
    drawRow(tr(STR_PACE_TREND), paceTrendValue);
    drawRow(tr(STR_PROGRESS_GAIN), progressGainValue);
    drawRow(tr(STR_CONFIDENCE), confidenceValue);
    drawRow(tr(STR_ESTIMATE_STABILITY), ReadingStatsAnalytics::formatEstimateStability(*book));
    drawRow(tr(STR_START_END_DATE), formatDateRange(book->firstReadAt, getCompletionDateForDisplay(*book)));

    renderer.fillRect(0, 0, pageWidth, contentTop, false);
    if (viewportBottom < pageHeight) {
      renderer.fillRect(0, viewportBottom, pageWidth, pageHeight - viewportBottom, false);
    }
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));

    storeBaseScreenBuffer();
  }

  const char* confirmLabel = Storage.exists(bookPath.c_str()) ? tr(STR_OPEN) : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (holdPreviewVisible) {
    drawHoldPreview(renderer, tr(STR_ADJUST_READING_TIME));
  }

  renderer.displayBuffer();
}
