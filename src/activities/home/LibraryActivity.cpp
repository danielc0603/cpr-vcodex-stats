#include "LibraryActivity.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <set>
#include <utility>
#include <variant>

#include "../util/ConfirmationActivity.h"
#include "../util/CompactHudRenderer.h"
#include "../util/KeyboardEntryActivity.h"
#include "../reader/ReaderBookInfoActivity.h"
#include "BookMetadataStore.h"
#include "CrossPointSettings.h"
#include "LibraryMetadataStore.h"
#include "ManualLibraryStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/file.h"
#include "components/icons/folder.h"
#include "components/icons/image24.h"
#include "components/icons/text.h"
#include "fontIds.h"
#include "library/LibraryCoverManager.h"
#include "library/LibraryCollections.h"
#include "library/LibraryIndex.h"
#include "library/LibrarySearch.h"
#include "util/BookIdentity.h"
#include "util/RecentBooksGrid.h"

std::string getFileExtension(std::string filename);
std::string getFileName(std::string filename);

namespace {
std::string titleFromPath(const std::string& path);
constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long HOLD_PREVIEW_MS = 250;
constexpr unsigned long BOOK_ACTION_HOLD_MS = 1400;
constexpr int BOOKSHELF_CARD_GAP = 8;
constexpr int BOOKSHELF_FOLDER_ICON_SIZE = 28;
constexpr int CARD_PAD = 12;
constexpr int COVER_GRID_PAD = 4;
constexpr int CARD_FOCUS_INSET = 4;
constexpr int LIBRARY_SECTION_LABEL_HEIGHT = 18;
constexpr int LIBRARY_TO_READ_SECTION_PAD = 6;
constexpr int LIBRARY_TO_READ_LABEL_GAP = 5;
constexpr int LIBRARY_ALL_BOOKS_LABEL_GAP = 5;
constexpr unsigned long LIBRARY_BACKGROUND_IDLE_MS = 650;
constexpr unsigned long LIBRARY_CACHED_COVER_IDLE_MS = 180;
constexpr unsigned long LIBRARY_POST_RENDER_WORK_IDLE_MS = 900;
constexpr int MAX_LIBRARY_ACTIVE_COVERS = 9;
constexpr int SHELF_COVER_WIDTH = RecentBooksGrid::kCoverWidth;
constexpr int SHELF_COVER_HEIGHT = RecentBooksGrid::kCoverHeight;
constexpr int SHELF_COVER_MIN_WIDTH = 54;
constexpr int SHELF_COVER_MIN_HEIGHT = 72;
constexpr uint8_t MEANINGFUL_PROGRESS_PERCENT = 2;
constexpr uint8_t LIBRARY_VIEW_DASHBOARD = 1;
constexpr uint8_t LIBRARY_VIEW_CONTINUE = 2;
constexpr uint8_t LIBRARY_VIEW_TO_READ = 3;
constexpr uint8_t LIBRARY_VIEW_FINISHED = 5;
constexpr uint8_t LIBRARY_VIEW_FILES = 7;
constexpr uint8_t LIBRARY_VIEW_AUTHOR = 8;
constexpr uint8_t LIBRARY_VIEW_SERIES = 9;
constexpr uint8_t LIBRARY_PROGRESS_NONE = 0;
constexpr uint8_t LIBRARY_PROGRESS_REFRESH = 1;
constexpr uint8_t LIBRARY_PROGRESS_REBUILD = 2;
constexpr uint8_t LIBRARY_PROGRESS_COVERS = 3;
constexpr uint8_t LIBRARY_INDEX_STAGE_IDLE = 0;
constexpr uint8_t LIBRARY_INDEX_STAGE_DISCOVERY = 1;
constexpr uint8_t LIBRARY_INDEX_STAGE_METADATA = 2;
constexpr uint8_t LIBRARY_INDEX_STAGE_FINALIZE = 3;
constexpr uint8_t LIBRARY_OVERLAY_NONE = 0;
constexpr uint8_t LIBRARY_OVERLAY_MENU = 1;
constexpr uint8_t LIBRARY_OVERLAY_COLLECTIONS = 2;
constexpr uint8_t LIBRARY_OVERLAY_SORT_BY = 3;
constexpr uint8_t LIBRARY_OVERLAY_SORT_DIRECTION = 4;
constexpr uint8_t LIBRARY_OVERLAY_BOOK_ACTIONS = 5;
constexpr size_t MAX_LIBRARY_DASHBOARD_BOOKS = 240;
constexpr int MAX_LIBRARY_SCAN_DEPTH = 8;
constexpr int MAX_LIBRARY_FILES_PER_TICK = 12;
constexpr int MAX_LIBRARY_FOLDERS_PER_TICK = 1;
constexpr uint8_t MAX_LIBRARY_FAILURES = 8;
constexpr size_t MAX_LIBRARY_SKIPPED_BOOK_DETAILS = 10;
constexpr uint32_t MIN_LIBRARY_COVER_HEAP = 70000;
constexpr char LIBRARY_BREADCRUMB_FILE[] = "/.crosspoint/library_crash_breadcrumb.txt";
constexpr char LIBRARY_DASHBOARD_INDEX_FILE[] = "/.crosspoint/library_dashboard.tsv";
constexpr char LIBRARY_DASHBOARD_INDEX_TMP[] = "/.crosspoint/library_dashboard.tmp";
constexpr char LIBRARY_DASHBOARD_SIGNATURE_FILE[] = "/.crosspoint/library_dashboard.sig";
constexpr char LIBRARY_INDEX_CHECKPOINT_FILE[] = "/.crosspoint/library_index_checkpoint.tsv";
constexpr char LIBRARY_INDEX_CHECKPOINT_TMP[] = "/.crosspoint/library_index_checkpoint.tmp";
constexpr char LIBRARY_RENDER_STATE_FILE[] = "/.crosspoint/library_render_state.tsv";
constexpr char LIBRARY_COVER_CACHE_FILE[] = "/.crosspoint/library_cover_cache.tsv";
constexpr int LIBRARY_DASHBOARD_SHORTCUT_COUNT = 0;
constexpr uint8_t LIBRARY_STATE_SECTION = 250;
constexpr uint8_t LIBRARY_FILTER_ALL = 0;
constexpr uint8_t LIBRARY_FILTER_TO_READ = 1;
constexpr uint8_t LIBRARY_FILTER_FINISHED = 2;
constexpr uint8_t ENTRY_TYPE_BOOK = 0;
constexpr uint8_t ENTRY_TYPE_AUTHOR_GROUP = 1;
constexpr uint8_t ENTRY_TYPE_SECTION_HEADER = 2;
constexpr uint8_t ENTRY_TYPE_PLACEHOLDER = 3;

void copyWarmupField(char* dest, const size_t destSize, const std::string& value) {
  if (dest == nullptr || destSize == 0) return;
  const size_t len = std::min(destSize - 1, value.size());
  if (len > 0) {
    std::memcpy(dest, value.data(), len);
  }
  dest[len] = '\0';
}

bool warmupFieldFits(const size_t destSize, const std::string& value) {
  return destSize > 0 && value.size() < destSize;
}

bool forceLibraryLayout3x3() {
  if (SETTINGS.bookshelfColumns == CrossPointSettings::BOOKSHELF_LAYOUT_3X3) {
    return false;
  }
  SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_LAYOUT_3X3;
  SETTINGS.saveToFile();
  return true;
}

enum BookAction : int {
  BOOK_ACTION_CONTINUE = 0,
  BOOK_ACTION_MARK_TO_READ = 1,
  BOOK_ACTION_MARK_FINISHED = 2,
  BOOK_ACTION_REMOVE_STATE = 3,
  BOOK_ACTION_DELETE = 4,
  BOOK_ACTION_ADD_TO_SERIES = 5,
  BOOK_ACTION_BOOK_INFO = 6,
  BOOK_ACTION_EDIT_TITLE = 7,
  BOOK_ACTION_EDIT_AUTHOR = 8,
  BOOK_ACTION_EDIT_TAGS = 9,
  BOOK_ACTION_EDIT_RATING = 10,
  BOOK_ACTION_EDIT_NOTES = 11,
  BOOK_ACTION_EDIT_SERIES_NUMBER = 12,
  BOOK_ACTION_REMOVE_FROM_SERIES = 13,
};

void drawHoldPreview(GfxRenderer& renderer, const char* text) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int horizontalPadding = 12;
  constexpr int verticalPadding = 6;
  constexpr int radius = 6;
  const int maxTextWidth = std::max(24, pageWidth - metrics.contentSidePadding * 2 - horizontalPadding * 2);
  const std::string safeText = renderer.truncatedText(SMALL_FONT_ID, text, maxTextWidth,
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

int libraryCardTextHeight(GfxRenderer& renderer) {
  return renderer.getLineHeight(SMALL_FONT_ID) * 3 + 14;
}

Rect libraryCardCoverArea(GfxRenderer& renderer, const Rect& inner) {
  const int textHeight = libraryCardTextHeight(renderer);
  return Rect{inner.x, inner.y, inner.width, std::max(SHELF_COVER_MIN_HEIGHT, inner.height - textHeight)};
}

void drawCenteredLibraryCardText(GfxRenderer& renderer, const Rect& inner, const Rect& coverArea,
                                 const std::string& title, const std::string& author, const uint8_t progress) {
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int textTop = coverArea.y + coverArea.height + 3;
  int y = textTop;
  const bool showProgress = progress > 0;
  const int progressReserve = showProgress ? 7 : 0;
  const auto titleLines =
      renderer.wrappedText(SMALL_FONT_ID, title.c_str(), inner.width, 2, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    if (y + lineHeight + progressReserve > inner.y + inner.height) break;
    const int lineW = renderer.getTextWidth(SMALL_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, inner.x + std::max(0, (inner.width - lineW) / 2), y, line.c_str(), true,
                      EpdFontFamily::BOLD);
    y += lineHeight;
  }
  if (!author.empty() && y + lineHeight + progressReserve <= inner.y + inner.height) {
    const std::string safeAuthor = renderer.truncatedText(SMALL_FONT_ID, author.c_str(), inner.width);
    const int authorW = renderer.getTextWidth(SMALL_FONT_ID, safeAuthor.c_str());
    renderer.drawText(SMALL_FONT_ID, inner.x + std::max(0, (inner.width - authorW) / 2), y, safeAuthor.c_str(), true);
    y += lineHeight;
  }
  if (showProgress) {
    const int barW = std::max(18, inner.width * 62 / 100);
    const int barH = 3;
    const int barX = inner.x + std::max(0, (inner.width - barW) / 2);
    const int barY = std::min(inner.y + inner.height - barH - 1, y + 2);
    if (barY >= inner.y && barY + barH <= inner.y + inner.height) {
      renderer.drawRect(barX, barY, barW, barH, true);
      const int fillW = std::clamp(static_cast<int>(progress) * (barW - 2) / 100, 1, std::max(1, barW - 2));
      renderer.fillRect(barX + 1, barY + 1, fillW, std::max(1, barH - 2), true);
    }
  }
}

enum LibraryCardState : uint8_t {
  LIBRARY_STATE_UNREAD = 0,
  LIBRARY_STATE_READING = 1,
  LIBRARY_STATE_FINISHED = 2,
  LIBRARY_STATE_TO_READ = 3,
  LIBRARY_STATE_PINNED = 4,
};

enum EntryCoverState : uint8_t {
  ENTRY_COVER_UNKNOWN = 0,
  ENTRY_COVER_READY = 1,
  ENTRY_COVER_MISSING = 2,
};

enum SortViewAction : int {
  SORT_VIEW_SORT = 0,
  SORT_VIEW_FILTER = 1,
  SORT_VIEW_REFRESH_LIBRARY = 2,
  SORT_VIEW_DIRECTION = 3,
  SORT_VIEW_TO_READ_SHELF = 4,
  SORT_VIEW_REFRESH_COVERS = 5,
  SORT_VIEW_RESET_LIBRARY = 6,
  SORT_VIEW_SEARCH = 7,
  SORT_VIEW_COLLECTIONS_SERIES = 8,
  SORT_VIEW_COLLECTIONS_AUTHOR = 9,
  SORT_VIEW_BROWSE_FILES = 10,
  SORT_VIEW_COLLECTIONS_TO_READ = 11,
  SORT_VIEW_COLLECTIONS_FINISHED = 12,
  SORT_VIEW_COLLECTIONS = 13,
  SORT_VIEW_COLUMNS = 20,
  SORT_VIEW_REFRESH = 30,
  SORT_VIEW_SORT_TITLE = 40,
  SORT_VIEW_SORT_RECENT = 41,
  SORT_VIEW_SORT_PROGRESS = 42,
  SORT_VIEW_DIRECTION_ASC = 43,
  SORT_VIEW_DIRECTION_DESC = 44,
};

std::string savedBrowserBasepath = "/";
uint8_t savedBrowserLibraryView = 0;
size_t savedBrowserSelectorIndex = 0;
bool hasSavedBrowserCursor = false;
uint8_t libraryFilterMode = LIBRARY_FILTER_ALL;
bool librarySortDescending = false;
std::string libraryAuthorViewKey;
std::string libraryAuthorViewName;
size_t libraryAuthorGroupSelectorIndex = 0;
std::string librarySearchQuery;
bool librarySearchSessionActive = false;
uint8_t librarySearchPreviousFilterMode = LIBRARY_FILTER_ALL;
uint8_t librarySearchPreviousView = LIBRARY_VIEW_DASHBOARD;
uint8_t librarySearchPreviousSort = CrossPointSettings::LIBRARY_SORT_TITLE;
bool librarySearchPreviousDescending = false;
size_t librarySearchPreviousSelectorIndex = 0;
std::string librarySearchPreviousAuthorKey;
std::string librarySearchPreviousAuthorName;
std::string librarySearchPreviousSeriesName;
std::string librarySeriesViewName;

struct LibraryDashboardSnapshot {
  std::vector<std::string> files;
  std::vector<uint8_t> completedFileStates;
  std::vector<uint8_t> progressFileStates;
  std::vector<uint32_t> entryRecentReadAt;
  std::vector<uint8_t> libraryFileStates;
  std::vector<uint16_t> folderItemCounts;
  std::vector<std::string> entryPaths;
  std::vector<std::string> entryTitles;
  std::vector<std::string> entrySubtitles;
  std::vector<std::string> entryCoverPaths;
  std::vector<std::string> entryCoverSourcePaths;
  std::vector<uint8_t> entryCoverStates;
  std::vector<uint8_t> entryTypes;
  std::vector<std::vector<std::string>> authorGroupCoverPaths;
  bool valid = false;
};

LibraryDashboardSnapshot& libraryDashboardSnapshot() {
  static LibraryDashboardSnapshot snapshot;
  return snapshot;
}

LibraryDashboardSnapshot& libraryAuthorGroupSnapshot() {
  static LibraryDashboardSnapshot snapshot;
  return snapshot;
}

struct ThumbnailCacheRecord {
  std::string stableBookId;
  std::string bookPath;
  std::string sourceCoverPath;
  std::string thumbPath;
  int width = 0;
  int height = 0;
  float aspectRatio = 0.0f;
  uint8_t state = ENTRY_COVER_UNKNOWN;
  bool failed = false;
  uint8_t version = 1;
};

std::vector<ThumbnailCacheRecord>& thumbnailCache() {
  static std::vector<ThumbnailCacheRecord> cache;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    const String body = Storage.readFile(LIBRARY_COVER_CACHE_FILE);
    std::string line;
    for (size_t i = 0; i <= body.length(); ++i) {
      const char ch = i < body.length() ? static_cast<char>(body[i]) : '\n';
      if (ch == '\n') {
        if (!line.empty()) {
          std::vector<std::string> fields;
          std::string field;
          for (char c : line) {
            if (c == '\t') {
              fields.push_back(field);
              field.clear();
            } else if (c != '\r') {
              field.push_back(c);
            }
          }
          fields.push_back(field);
          if (fields.size() >= 10) {
            ThumbnailCacheRecord record;
            record.version = static_cast<uint8_t>(std::max(1, atoi(fields[0].c_str())));
            record.stableBookId = fields[1];
            record.bookPath = fields[2];
            record.sourceCoverPath = fields[3];
            record.thumbPath = fields[4];
            record.width = atoi(fields[5].c_str());
            record.height = atoi(fields[6].c_str());
            record.aspectRatio = static_cast<float>(atof(fields[7].c_str()));
            record.state = static_cast<uint8_t>(atoi(fields[8].c_str()));
            record.failed = atoi(fields[9].c_str()) != 0;
            if (!record.bookPath.empty()) {
              cache.push_back(record);
            }
          }
        }
        line.clear();
      } else {
        line.push_back(ch);
      }
    }
  }
  return cache;
}

void saveThumbnailCache() {
  String body;
  for (const auto& record : thumbnailCache()) {
    body += String(static_cast<int>(record.version)) + "\t";
    body += record.stableBookId.c_str();
    body += "\t";
    body += record.bookPath.c_str();
    body += "\t";
    body += record.sourceCoverPath.c_str();
    body += "\t";
    body += record.thumbPath.c_str();
    body += "\t";
    body += String(record.width) + "\t";
    body += String(record.height) + "\t";
    body += String(record.aspectRatio, 4) + "\t";
    body += String(static_cast<int>(record.state)) + "\t";
    body += String(record.failed ? 1 : 0) + "\n";
  }
  Storage.writeFile(LIBRARY_COVER_CACHE_FILE, body);
}

void pruneInvalidThumbnailCacheRecords() {
  auto& cache = thumbnailCache();
  const size_t before = cache.size();
  cache.erase(std::remove_if(cache.begin(), cache.end(), [](const ThumbnailCacheRecord& record) {
                return record.state == ENTRY_COVER_READY &&
                       (record.thumbPath.empty() || !Storage.exists(record.thumbPath.c_str()));
              }),
              cache.end());
  if (cache.size() != before) {
    saveThumbnailCache();
  }
}

ThumbnailCacheRecord* findThumbnailCacheRecord(const std::string& bookPath, const std::string& sourceCoverPath,
                                                const int width, const int height) {
  auto& cache = thumbnailCache();
  for (auto& record : cache) {
    if (record.bookPath == bookPath && record.sourceCoverPath == sourceCoverPath && record.width == width &&
        record.height == height) {
      return &record;
    }
  }
  return nullptr;
}

ThumbnailCacheRecord* findReusableThumbnailCacheRecord(const std::string& bookPath,
                                                       const std::string& sourceCoverPath,
                                                       const int width, const int height, bool* updatedRecord = nullptr) {
  if (updatedRecord != nullptr) {
    *updatedRecord = false;
  }
  if (auto* exact = findThumbnailCacheRecord(bookPath, sourceCoverPath, width, height)) {
    return exact;
  }
  const std::string stableBookId = BookIdentity::resolveStableBookId(bookPath);
  if (stableBookId.empty()) {
    return nullptr;
  }
  auto& cache = thumbnailCache();
  for (auto& record : cache) {
    if (record.stableBookId != stableBookId || record.width != width || record.height != height) {
      continue;
    }
    if (record.state == ENTRY_COVER_READY && !record.thumbPath.empty() && Storage.exists(record.thumbPath.c_str())) {
      record.bookPath = bookPath;
      record.sourceCoverPath = sourceCoverPath;
      if (updatedRecord != nullptr) {
        *updatedRecord = true;
      }
      return &record;
    }
  }
  return nullptr;
}

void rememberThumbnailCacheRecord(const std::string& bookPath, const std::string& sourceCoverPath,
                                  const std::string& thumbPath, const int width, const int height,
                                  const uint8_t state) {
  auto& cache = thumbnailCache();
  const std::string stableBookId = BookIdentity::resolveStableBookId(bookPath);
  if (auto* existing = findThumbnailCacheRecord(bookPath, sourceCoverPath, width, height)) {
    existing->stableBookId = stableBookId;
    existing->thumbPath = thumbPath;
    existing->state = state;
    existing->failed = state == ENTRY_COVER_MISSING;
    existing->aspectRatio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
    saveThumbnailCache();
    return;
  }
  if (!stableBookId.empty()) {
    for (auto& record : cache) {
      if (record.stableBookId == stableBookId && record.width == width && record.height == height) {
        record.bookPath = bookPath;
        record.sourceCoverPath = sourceCoverPath;
        record.thumbPath = thumbPath;
        record.state = state;
        record.failed = state == ENTRY_COVER_MISSING;
        record.aspectRatio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f;
        saveThumbnailCache();
        return;
      }
    }
  }
  constexpr size_t kMaxThumbnailCacheRecords = 96;
  if (cache.size() >= kMaxThumbnailCacheRecords) {
    cache.erase(cache.begin());
  }
  cache.push_back(ThumbnailCacheRecord{stableBookId, bookPath, sourceCoverPath, thumbPath, width, height,
                                       height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f,
                                       state, state == ENTRY_COVER_MISSING, 1});
  saveThumbnailCache();
}

const uint8_t* fileIconBitmap(const std::string& filename) {
  if (!filename.empty() && filename.back() == '/') {
    return FolderIcon;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return BookIcon;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return TextIcon;
  }
  if (FsHelpers::hasBmpExtension(filename)) {
    return Image24Icon;
  }
  return FileIcon;
}

bool isLibraryBookCandidate(std::string_view filename) {
  return FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
         FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename);
}

std::string fileTypeLabel(const std::string& filename) {
  if (!filename.empty() && filename.back() == '/') {
    return "Folder";
  }
  std::string extension = getFileExtension(filename);
  if (!extension.empty() && extension.front() == '.') {
    extension.erase(extension.begin());
  }
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return extension.empty() ? "File" : extension;
}

const char* libraryLayoutLabel() {
  return tr(STR_LAYOUT_3X3);
}

const char* librarySortLabel() {
  switch (SETTINGS.librarySort) {
    case CrossPointSettings::LIBRARY_SORT_AUTHOR:
      return tr(STR_AUTHOR);
    case CrossPointSettings::LIBRARY_SORT_RECENT:
      return tr(STR_RECENT_BOOKS);
    case CrossPointSettings::LIBRARY_SORT_PROGRESS:
      return tr(STR_PROGRESS);
    case CrossPointSettings::LIBRARY_SORT_TITLE:
    default:
      return tr(STR_TITLE);
  }
}

const char* libraryFilterLabel() {
  switch (libraryFilterMode) {
    case LIBRARY_FILTER_TO_READ:
      return tr(STR_TO_READ);
    case LIBRARY_FILTER_FINISHED:
      return tr(STR_FINISHED_BOOKS);
    case LIBRARY_FILTER_ALL:
    default:
      return tr(STR_ALL_BOOKS);
  }
}

const char* librarySortDirectionLabel() {
  return librarySortDescending ? tr(STR_SORT_DESC) : tr(STR_SORT_ASC);
}

const char* libraryToReadShelfLabel() {
  return SETTINGS.showToReadShelf ? tr(STR_ON) : tr(STR_OFF);
}

void cycleLibrarySortSetting() {
  switch (SETTINGS.librarySort) {
    case CrossPointSettings::LIBRARY_SORT_TITLE:
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_RECENT;
      break;
    case CrossPointSettings::LIBRARY_SORT_RECENT:
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_PROGRESS;
      break;
    case CrossPointSettings::LIBRARY_SORT_PROGRESS:
    default:
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_TITLE;
      break;
  }
}

int libraryCoverTargetWidth() {
  return 108;
}

int libraryCoverTargetHeight() {
  return 172;
}

std::string lowercaseCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

int naturalCompare(const std::string& a, const std::string& b) {
  const char* s1 = a.c_str();
  const char* s2 = b.c_str();
  while (*s1 && *s2) {
    const auto uc = [](char c) { return static_cast<unsigned char>(c); };
    if (std::isdigit(uc(*s1)) && std::isdigit(uc(*s2))) {
      while (*s1 == '0') ++s1;
      while (*s2 == '0') ++s2;
      int len1 = 0;
      int len2 = 0;
      while (std::isdigit(uc(s1[len1]))) ++len1;
      while (std::isdigit(uc(s2[len2]))) ++len2;
      if (len1 != len2) return len1 < len2 ? -1 : 1;
      for (int index = 0; index < len1; ++index) {
        if (s1[index] != s2[index]) return s1[index] < s2[index] ? -1 : 1;
      }
      s1 += len1;
      s2 += len2;
      continue;
    }
    const char c1 = static_cast<char>(std::tolower(uc(*s1)));
    const char c2 = static_cast<char>(std::tolower(uc(*s2)));
    if (c1 != c2) return c1 < c2 ? -1 : 1;
    ++s1;
    ++s2;
  }
  if (*s1 == *s2) return 0;
  return *s1 == '\0' ? -1 : 1;
}

bool naturalLess(const std::string& a, const std::string& b, const bool descending = false) {
  const int cmp = naturalCompare(a, b);
  return descending ? cmp > 0 : cmp < 0;
}

std::string trimCopy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string parentFolderName(const std::string& path) {
  if (path.empty()) return "";
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "";
  const size_t parentEnd = slash - 1;
  const size_t parentStart = path.find_last_of('/', parentEnd);
  const size_t start = parentStart == std::string::npos ? 0 : parentStart + 1;
  return path.substr(start, parentEnd - start + 1);
}

bool splitAuthorTitleFromPath(const std::string& path, std::string& author, std::string& title) {
  const size_t slash = path.find_last_of('/');
  const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
  const std::string base = getFileName(filename);
  const size_t sep = base.find(" - ");
  if (sep == std::string::npos || sep == 0 || sep + 3 >= base.size()) {
    return false;
  }
  author = trimCopy(base.substr(0, sep));
  title = trimCopy(base.substr(sep + 3));
  return !author.empty() && !title.empty();
}

std::string titleCaseWords(std::string value) {
  bool nextUpper = true;
  for (char& ch : value) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch)) {
      ch = static_cast<char>(nextUpper ? std::toupper(uch) : std::tolower(uch));
      nextUpper = false;
    } else {
      nextUpper = true;
    }
  }
  return value;
}

std::vector<std::string> authorTokens(const std::string& value) {
  std::vector<std::string> tokens;
  std::string token;
  for (const char ch : lowercaseCopy(value)) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch)) {
      token.push_back(ch);
    } else if (!token.empty()) {
      tokens.push_back(token);
      token.clear();
    }
  }
  if (!token.empty()) tokens.push_back(token);
  return tokens;
}

std::string normalizedAuthorSource(const std::string& cachedAuthor, const std::string& path) {
  std::string author = trimCopy(cachedAuthor);
  if (author.empty()) {
    std::string parsedAuthor;
    std::string parsedTitle;
    if (splitAuthorTitleFromPath(path, parsedAuthor, parsedTitle)) {
      author = parsedAuthor;
    }
  }
  const std::string loweredAuthor = lowercaseCopy(author);
  if (author.empty() || loweredAuthor == "books" || loweredAuthor == "book" || loweredAuthor == "library") {
    author = trimCopy(parentFolderName(path));
    const std::string loweredParent = lowercaseCopy(author);
    if (loweredParent == "books" || loweredParent == "book" || loweredParent == "library") {
      author.clear();
    }
  }
  return author;
}

std::string canonicalAuthorSortKey(const std::string& author) {
  auto tokens = authorTokens(author);
  if (tokens.empty()) return "";
  if (tokens.size() == 2) {
    std::sort(tokens.begin(), tokens.end());
  }
  std::string key;
  for (const auto& token : tokens) {
    if (!key.empty()) key += "|";
    key += token;
  }
  return key;
}

std::string canonicalAuthorDisplayName(const std::string& author) {
  std::string cleaned = trimCopy(author);
  if (cleaned.empty()) return tr(STR_UNKNOWN_AUTHOR);
  const size_t comma = cleaned.find(',');
  if (comma != std::string::npos) {
    const std::string last = trimCopy(cleaned.substr(0, comma));
    const std::string first = trimCopy(cleaned.substr(comma + 1));
    if (!first.empty() && !last.empty()) {
      cleaned = first + " " + last;
    }
  }
  return titleCaseWords(cleaned);
}

std::string authorSortKey(const std::string& cachedAuthor, const std::string& path, const std::string& title) {
  std::string author = normalizedAuthorSource(cachedAuthor, path);
  if (author.empty()) {
    author = trimCopy(title);
  }
  return canonicalAuthorSortKey(author);
}

std::string authorDisplayName(const std::string& cachedAuthor, const std::string& path) {
  return canonicalAuthorDisplayName(normalizedAuthorSource(cachedAuthor, path));
}

std::string joinLibraryValues(const std::vector<std::string>& values) {
  std::string joined;
  for (const auto& value : values) {
    if (value.empty()) continue;
    if (!joined.empty()) joined += " ";
    joined += value;
  }
  return joined;
}

struct LibraryResolvedBookInfo {
  std::string title;
  std::string author;
  std::string coverPath;
  std::string seriesName;
  std::string seriesIndex;
  std::string searchText;
};

LibraryResolvedBookInfo resolveLibraryBookInfo(const std::string& path, const std::string& stableBookId,
                                               const LibraryBookMetadata* libraryMetadata,
                                               const CachedBookMetadata* importedMetadata,
                                               const ReadingBookStats* statsBook) {
  const auto* manual = MANUAL_LIBRARY.findBook(path, stableBookId);
  LibraryResolvedBookInfo info;
  if (manual != nullptr) {
    info.title = manual->manualTitle;
    info.author = manual->manualAuthor;
    info.coverPath = manual->customCover.empty() ? manual->customThumbnail : manual->customCover;
    info.seriesName = manual->manualSeries;
    info.seriesIndex = manual->manualSeriesNumber;
    info.searchText += " " + joinLibraryValues(manual->personalTags) + " " + manual->notes;
  }
  if (libraryMetadata != nullptr) {
    if (info.title.empty()) info.title = libraryMetadata->title;
    if (info.author.empty()) info.author = libraryMetadata->author;
    if (info.coverPath.empty()) info.coverPath = libraryMetadata->coverPath;
    if (info.seriesName.empty()) info.seriesName = libraryMetadata->seriesName;
    if (info.seriesIndex.empty()) info.seriesIndex = libraryMetadata->seriesIndex;
    info.searchText += " " + libraryMetadata->subtitle + " " + libraryMetadata->publisher + " " +
                       libraryMetadata->language + " " + libraryMetadata->seriesName + " " +
                       libraryMetadata->genre + " " + libraryMetadata->subjects + " " + libraryMetadata->tags + " " +
                       libraryMetadata->keywords + " " + libraryMetadata->description + " " +
                       libraryMetadata->isbn + " " + libraryMetadata->asin + " " + libraryMetadata->doi;
  }
  if (importedMetadata != nullptr) {
    if (info.title.empty()) info.title = importedMetadata->title;
    if (info.author.empty()) info.author = importedMetadata->author;
    if (info.coverPath.empty()) info.coverPath = importedMetadata->coverPath;
    if (info.seriesName.empty()) info.seriesName = importedMetadata->series;
    if (info.seriesIndex.empty()) info.seriesIndex = importedMetadata->seriesIndex;
    info.searchText += " " + importedMetadata->series + " " + importedMetadata->tags + " " +
                       importedMetadata->publisher + " " + importedMetadata->language + " " +
                       importedMetadata->description + " " + importedMetadata->identifier;
  }
  if (statsBook != nullptr) {
    if (info.title.empty()) info.title = statsBook->title;
    if (info.author.empty()) info.author = statsBook->author;
    if (info.coverPath.empty()) info.coverPath = statsBook->coverBmpPath;
  }
  if (info.title.empty()) info.title = titleFromPath(path);
  if (info.author.empty()) info.author = authorDisplayName("", path);
  info.searchText += " " + info.title + " " + info.author + " " + info.seriesName;
  return info;
}

bool pathHasFinishedFolder(const std::string& path) {
  size_t start = 0;
  while (start < path.size()) {
    while (start < path.size() && path[start] == '/') {
      ++start;
    }
    size_t end = path.find('/', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    if (end > start) {
      const std::string segment = lowercaseCopy(path.substr(start, end - start));
      if (segment == "read" || segment == "completed" || segment == "finished") {
        return true;
      }
    }
    start = end + 1;
  }
  return false;
}

uint8_t classifyLibraryBookState(const std::string& path, const LibraryBookMetadata* metadata,
                                 const ReadingBookStats* statsBook) {
  const uint8_t progress = statsBook != nullptr ? statsBook->lastProgressPercent : 0;
  if ((metadata != nullptr && metadata->finished) || (statsBook != nullptr && statsBook->completed) ||
      pathHasFinishedFolder(path)) {
    return LIBRARY_STATE_FINISHED;
  }
  if (metadata != nullptr && metadata->toRead && progress < MEANINGFUL_PROGRESS_PERCENT) {
    return LIBRARY_STATE_TO_READ;
  }
  if (metadata != nullptr && metadata->pinned) {
    return LIBRARY_STATE_PINNED;
  }
  if (progress >= MEANINGFUL_PROGRESS_PERCENT && (metadata == nullptr || !metadata->activeRemoved)) {
    return LIBRARY_STATE_READING;
  }
  return LIBRARY_STATE_UNREAD;
}

std::string titleFromPath(const std::string& path) {
  std::string parsedAuthor;
  std::string parsedTitle;
  if (splitAuthorTitleFromPath(path, parsedAuthor, parsedTitle)) {
    return parsedTitle;
  }
  const size_t slash = path.find_last_of('/');
  const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
  return getFileName(filename);
}

std::string escapeIndexField(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    if (ch == '\\' || ch == '\t' || ch == '\n' || ch == '\r') {
      escaped.push_back('\\');
      switch (ch) {
        case '\t':
          escaped.push_back('t');
          break;
        case '\n':
          escaped.push_back('n');
          break;
        case '\r':
          escaped.push_back('r');
          break;
        default:
          escaped.push_back(ch);
          break;
      }
    } else {
      escaped.push_back(ch);
    }
  }
  return escaped;
}

std::string unescapeIndexField(const std::string& value) {
  std::string unescaped;
  unescaped.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    const char ch = value[i];
    if (ch == '\\' && i + 1 < value.size()) {
      const char next = value[++i];
      switch (next) {
        case 't':
          unescaped.push_back('\t');
          break;
        case 'n':
          unescaped.push_back('\n');
          break;
        case 'r':
          unescaped.push_back('\r');
          break;
        default:
          unescaped.push_back(next);
          break;
      }
    } else {
      unescaped.push_back(ch);
    }
  }
  return unescaped;
}

std::vector<std::string> splitIndexLine(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool escaping = false;
  for (const char ch : line) {
    if (escaping) {
      field.push_back('\\');
      field.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '\t') {
      fields.push_back(unescapeIndexField(field));
      field.clear();
      continue;
    }
    field.push_back(ch);
  }
  if (escaping) {
    field.push_back('\\');
  }
  fields.push_back(unescapeIndexField(field));
  return fields;
}

bool readIndexLine(FsFile& file, std::string& line) {
  line.clear();
  while (file.available()) {
    const int value = file.read();
    if (value < 0) {
      break;
    }
    const char ch = static_cast<char>(value);
    if (ch == '\n') {
      return true;
    }
    if (ch != '\r') {
      line.push_back(ch);
    }
  }
  return !line.empty();
}

bool isIgnoredLibraryFolder(const std::string& folderName);

std::string computeLibraryRootSignature() {
  auto root = Storage.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return "";
  }

  uint16_t rootEpubCount = 0;
  uint16_t rootFolderCount = 0;
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    const bool hidden = (!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0;
    if (hidden) {
      file.close();
      continue;
    }
    if (file.isDirectory()) {
      if (!isIgnoredLibraryFolder(name) && rootFolderCount < 65535) {
        ++rootFolderCount;
      }
    } else {
      std::string_view filename{name};
      if (FsHelpers::hasEpubExtension(filename) && rootEpubCount < 65535) {
        ++rootEpubCount;
      }
    }
    file.close();
  }
  root.close();
  return "v1\t" + std::to_string(rootEpubCount) + "\t" + std::to_string(rootFolderCount);
}

bool isIgnoredLibraryFolder(const std::string& folderName) {
  std::string name = folderName;
  while (!name.empty() && name.back() == '/') {
    name.pop_back();
  }
  const size_t slash = name.find_last_of('/');
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }
  std::transform(name.begin(), name.end(), name.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return name == "xtcache" || name == ".crosspoint" || name == ".system" ||
         name == ".cpr-vcodex-stats-backup" || name == "exports" || name == "font" ||
         name == "fonts";
}

bool pathHasIgnoredLibraryFolder(const std::string& path) {
  size_t start = 0;
  while (start < path.size()) {
    while (start < path.size() && path[start] == '/') {
      ++start;
    }
    size_t end = path.find('/', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    if (end > start && isIgnoredLibraryFolder(path.substr(start, end - start))) {
      return true;
    }
    start = end + 1;
  }
  return false;
}

Rect insetRect(const Rect& rect, const int inset) {
  return Rect{rect.x + inset, rect.y + inset, std::max(0, rect.width - inset * 2),
              std::max(0, rect.height - inset * 2)};
}

void drawContainedCard(GfxRenderer& renderer, const Rect& card, const bool selected, const int radius = 6) {
  if (selected) {
    const Rect focus = insetRect(card, -3);
    renderer.fillRoundedRect(focus.x, focus.y, focus.width, focus.height, std::max(4, radius + 2), Color::LightGray);
  }
  renderer.fillRoundedRect(card.x, card.y, card.width, card.height, radius, Color::White);
  if (selected) {
    const Rect focus = insetRect(card, -3);
    renderer.drawRoundedRect(focus.x, focus.y, focus.width, focus.height, 3, std::max(4, radius + 2), true);
  }
}

void drawFolderGlyph(GfxRenderer& renderer, const Rect& rect) {
  if (rect.width <= 0 || rect.height <= 0) return;
  const int tabH = std::max(5, rect.height / 5);
  const int tabW = std::max(12, rect.width * 42 / 100);
  const int bodyY = rect.y + tabH;
  const int bodyH = std::max(8, rect.height - tabH);
  renderer.fillRoundedRect(rect.x, bodyY, rect.width, bodyH, 4, Color::LightGray);
  renderer.fillRoundedRect(rect.x + 1, rect.y, tabW, tabH + 4, 3, Color::LightGray);
  renderer.drawRoundedRect(rect.x, bodyY, rect.width, bodyH, 2, 4, true);
  renderer.drawRoundedRect(rect.x + 1, rect.y, tabW, tabH + 4, 2, 3, true);
  renderer.drawLine(rect.x + tabW - 1, bodyY, rect.x + rect.width - 4, bodyY, 2, true);
}

void drawBookPlaceholder(GfxRenderer& renderer, const Rect& rect, const bool imageFile) {
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, RecentBooksGrid::kCoverCornerRadius,
                           Color::White);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, RecentBooksGrid::kCoverCornerRadius, true);
  if (imageFile) {
    constexpr int iconSize = 24;
    renderer.drawIcon(Image24Icon, rect.x + (rect.width - iconSize) / 2,
                      rect.y + std::max(6, (rect.height - iconSize) / 2), iconSize, iconSize);
    return;
  }

  const int bookW = std::min(std::max(28, rect.width - 18), 44);
  const int bookH = std::min(std::max(38, rect.height - 20), 58);
  const int bookX = rect.x + (rect.width - bookW) / 2;
  const int bookY = rect.y + (rect.height - bookH) / 2;
  renderer.drawRoundedRect(bookX, bookY, bookW, bookH, 2, 4, true);
  renderer.drawLine(bookX + 8, bookY + 4, bookX + 8, bookY + bookH - 5, true);
  renderer.drawLine(bookX + 13, bookY + 13, bookX + bookW - 8, bookY + 13, true);
  renderer.drawLine(bookX + 13, bookY + 23, bookX + bookW - 11, bookY + 23, true);
  renderer.drawLine(bookX + 13, bookY + bookH - 12, bookX + bookW - 13, bookY + bookH - 12, true);
}

Rect calculateBookCoverRect(const Rect& card, const Rect& inner, const int statusStripTop, const int titleReserve,
                            const int subtitleReserve) {
  const int availableHeight =
      std::max(SHELF_COVER_MIN_HEIGHT, statusStripTop - inner.y - titleReserve - subtitleReserve);
  const int targetHeight = libraryCoverTargetHeight();
  const int targetWidth = libraryCoverTargetWidth();
  const int maxCoverHeight = std::max(SHELF_COVER_MIN_HEIGHT, std::min(targetHeight, availableHeight));
  const int maxCoverWidth = std::max(SHELF_COVER_MIN_WIDTH, inner.width);

  int coverHeight = maxCoverHeight;
  int coverWidth = coverHeight * SHELF_COVER_WIDTH / SHELF_COVER_HEIGHT;
  if (coverWidth > maxCoverWidth || coverWidth > targetWidth) {
    coverWidth = std::min(maxCoverWidth, targetWidth);
    coverHeight = std::max(SHELF_COVER_MIN_HEIGHT, coverWidth * SHELF_COVER_HEIGHT / SHELF_COVER_WIDTH);
  }

  return Rect{card.x + (card.width - coverWidth) / 2, inner.y, coverWidth, coverHeight};
}

class SeriesPickerActivity final : public Activity {
  static constexpr int CREATE_NEW_ACTION = -2;

  std::vector<std::string> seriesNames;
  int selectedIndex = 0;
  ButtonNavigator navigator;

 public:
  SeriesPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<std::string> names)
      : Activity("SeriesPicker", renderer, mappedInput), seriesNames(std::move(names)) {}

  void onEnter() override {
    Activity::onEnter();
    requestUpdate(true);
  }

  void loop() override {
    const int count = static_cast<int>(seriesNames.size()) + 1;
    navigator.onNext([this, count] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
      requestUpdate();
    });
    navigator.onPrevious([this, count] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedIndex == static_cast<int>(seriesNames.size())) {
        setResult(MenuResult{CREATE_NEW_ACTION, 0, 0});
      } else {
        setResult(KeyboardResult{seriesNames[selectedIndex]});
      }
      finish();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();
    const int count = static_cast<int>(seriesNames.size()) + 1;
    std::vector<CompactHudRenderer::Row> rows;
    rows.reserve(count);
    for (int index = 0; index < count; ++index) {
      if (index == static_cast<int>(seriesNames.size())) {
        rows.push_back({tr(STR_CREATE_NEW_SERIES), ""});
      } else {
        rows.push_back({seriesNames[index].empty() ? std::string(tr(STR_NO_SERIES)) : seriesNames[index], ""});
      }
    }
    CompactHudRenderer::ActionListConfig config;
    config.title = tr(STR_SELECT_SERIES);
    config.rows = std::move(rows);
    config.selectedIndex = selectedIndex;
    config.minWidth = 340;
    config.maxRows = 8;
    config.wrapRows = true;
    CompactHudRenderer::drawActionList(renderer, mappedInput, config);
  }

  static constexpr int createNewAction() { return CREATE_NEW_ACTION; }
};
}  // namespace

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    // Directories first
    bool isDir1 = str1.back() == '/';
    bool isDir2 = str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    // Start naive natural sort
    const char* s1 = str1.c_str();
    const char* s2 = str2.c_str();

    // Iterate while both strings have characters
    while (*s1 && *s2) {
      const auto uc = [](char c) { return static_cast<unsigned char>(c); };
      // Check if both are at the start of a number
      if (std::isdigit(uc(*s1)) && std::isdigit(uc(*s2))) {
        // Skip leading zeros and track them
        const char* start1 = s1;
        const char* start2 = s2;
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;

        // Count digits to compare lengths first
        int len1 = 0, len2 = 0;
        while (std::isdigit(uc(s1[len1]))) len1++;
        while (std::isdigit(uc(s2[len2]))) len2++;

        // Different length so return smaller integer value
        if (len1 != len2) return len1 < len2;

        // Same length so compare digit by digit
        for (int i = 0; i < len1; i++) {
          if (s1[i] != s2[i]) return s1[i] < s2[i];
        }

        // Numbers equal so advance pointers
        s1 += len1;
        s2 += len2;
      } else {
        // Regular case-insensitive character comparison
        char c1 = std::tolower(uc(*s1));
        char c2 = std::tolower(uc(*s2));
        if (c1 != c2) return c1 < c2;
        s1++;
        s2++;
      }
    }

    // One string is prefix of other
    return *s1 == '\0' && *s2 != '\0';
  });
}

void LibraryActivity::loadFiles() {
  libraryHotRowsReadyMs = 0;
  librarySelectionOnlyRender = false;
  libraryHasRetainedGridRects = false;
  libraryRetainedItemRects.clear();
  libraryRetainedMetadataRect = Rect{};
  libraryRetainedRenderToken.clear();
  libraryRetainedContentHeight = 0;
  files.clear();
  completedFileStates.clear();
  progressFileStates.clear();
  entryRecentReadAt.clear();
  libraryFileStates.clear();
  folderItemCounts.clear();
  entryPaths.clear();
  entryTitles.clear();
  entrySubtitles.clear();
  entryCoverPaths.clear();
  entryCoverSourcePaths.clear();
  entryCoverStates.clear();
  entryTypes.clear();
  authorGroupCoverPaths.clear();
  resolvedRows.clear();
  libraryScanFolders.clear();
  libraryScanOffsets.clear();
  libraryCurrentScanFolder.clear();
  libraryIndexingActive = false;

  if (isBookshelfMode() && rawFilesLaunch) {
    libraryView = LIBRARY_VIEW_FILES;
  }
  if (!usesBookshelfGrid()) {
    libraryCoverManager.clearWindows();
  }
  if (isBookshelfMode() && basepath == "/" && libraryView == 0) {
    libraryView = LIBRARY_VIEW_DASHBOARD;
  }
  if (isBookshelfMode() && basepath == "/" && isLibraryDashboard()) {
    loadLibraryDashboard();
    clampSelector();
    rebuildResolvedLibraryRows();
    libraryHotRowsReadyMs = millis();
    return;
  }
  if (isBookshelfMode() && basepath == "/" && isLibraryShelf()) {
    loadLibraryShelf(libraryView);
    clampSelector();
    rebuildResolvedLibraryRows();
    libraryHotRowsReadyMs = millis();
    return;
  }
  if (isBookshelfMode() && basepath == "/" && isLibraryAuthorView()) {
    loadLibraryShelf(libraryView);
    clampSelector();
    rebuildResolvedLibraryRows();
    libraryHotRowsReadyMs = millis();
    return;
  }
  if (isBookshelfMode() && basepath == "/" && isLibrarySeriesView()) {
    loadLibraryShelf(libraryView);
    clampSelector();
    rebuildResolvedLibraryRows();
    libraryHotRowsReadyMs = millis();
    return;
  }

  loadFilesystemFiles();
  clampSelector();
  rebuildResolvedLibraryRows();
  libraryHotRowsReadyMs = millis();
}

void LibraryActivity::loadFilesystemFiles() {
  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
    } else {
      std::string_view filename{name};
      if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
          FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
          FsHelpers::hasBmpExtension(filename)) {
        files.emplace_back(filename);
      }
    }
    file.close();
  }
  root.close();
  sortFileList(files);

  std::string fullPathPrefix = basepath;
  if (fullPathPrefix.empty() || fullPathPrefix.back() != '/') {
    fullPathPrefix += "/";
  }

  std::vector<std::string> visibleFiles;
  visibleFiles.reserve(files.size());
  for (const auto& entry : files) {
    if (entry.empty() || entry.back() == '/') {
      visibleFiles.push_back(entry);
      continue;
    }

    const std::string fullPath = fullPathPrefix + entry;
    const auto* statsBook = READING_STATS.findBook(fullPath);
    const auto* metadata = LIBRARY_METADATA.findBook(fullPath);
    if ((metadata != nullptr && metadata->finished) || (statsBook != nullptr && statsBook->completed)) {
      continue;
    }
    visibleFiles.push_back(entry);
  }
  files = std::move(visibleFiles);

  completedFileStates.reserve(files.size());
  progressFileStates.reserve(files.size());
  entryRecentReadAt.reserve(files.size());
  libraryFileStates.reserve(files.size());
  folderItemCounts.reserve(files.size());

  for (const auto& entry : files) {
    if (entry.empty() || entry.back() == '/') {
      completedFileStates.push_back(0);
      progressFileStates.push_back(0);
      entryRecentReadAt.push_back(0);
      libraryFileStates.push_back(LIBRARY_STATE_UNREAD);
      folderItemCounts.push_back(countFolderItems(entry));
      entryPaths.push_back(fullPathPrefix + entry);
      entryTitles.push_back(getFileName(entry));
      entrySubtitles.emplace_back();
      addEntryCoverPlaceholder();
      entryTypes.push_back(ENTRY_TYPE_BOOK);
      authorGroupCoverPaths.emplace_back();
      continue;
    }

    const std::string fullPath = fullPathPrefix + entry;
    const auto* statsBook = READING_STATS.findBook(fullPath);
    const auto* metadata = LIBRARY_METADATA.findBook(fullPath);
    if (metadata != nullptr && metadata->toRead && statsBook != nullptr &&
        statsBook->lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT) {
      LIBRARY_METADATA.removeFromToRead(fullPath);
      metadata = LIBRARY_METADATA.findBook(fullPath);
    }
    completedFileStates.push_back((statsBook != nullptr && statsBook->completed) ? 1 : 0);
    progressFileStates.push_back(statsBook != nullptr ? statsBook->lastProgressPercent : 0);
    entryRecentReadAt.push_back(statsBook != nullptr ? statsBook->lastReadAt : 0);
    uint8_t libraryState = LIBRARY_STATE_UNREAD;
    if ((metadata != nullptr && metadata->finished) || (statsBook != nullptr && statsBook->completed)) {
      libraryState = LIBRARY_STATE_FINISHED;
    } else if (metadata != nullptr && metadata->toRead &&
               (statsBook == nullptr || statsBook->lastProgressPercent < MEANINGFUL_PROGRESS_PERCENT)) {
      libraryState = LIBRARY_STATE_TO_READ;
    } else if (metadata != nullptr && metadata->pinned) {
      libraryState = LIBRARY_STATE_PINNED;
    } else if (statsBook != nullptr && statsBook->lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT &&
               (metadata == nullptr || !metadata->activeRemoved)) {
      libraryState = LIBRARY_STATE_READING;
    }
    libraryFileStates.push_back(libraryState);
    folderItemCounts.push_back(0);
    entryPaths.push_back(fullPath);
    entryTitles.push_back(getFileName(entry));
    entrySubtitles.emplace_back();
    addEntryCoverPlaceholder();
    entryTypes.push_back(ENTRY_TYPE_BOOK);
    authorGroupCoverPaths.emplace_back();
  }
}

bool LibraryActivity::isLibraryDashboard() const { return libraryView == LIBRARY_VIEW_DASHBOARD; }

bool LibraryActivity::isLibraryShelf() const {
  return libraryView == LIBRARY_VIEW_CONTINUE || libraryView == LIBRARY_VIEW_TO_READ ||
         libraryView == LIBRARY_VIEW_FINISHED;
}

bool LibraryActivity::isLibraryAuthorView() const { return libraryView == LIBRARY_VIEW_AUTHOR; }

bool LibraryActivity::isLibrarySeriesView() const { return libraryView == LIBRARY_VIEW_SERIES; }

bool LibraryActivity::isRawBrowseFilesMode() const {
  return rawFilesLaunch || (isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES);
}

bool LibraryActivity::usesBookshelfGrid() const { return isBookshelfMode() && !isRawBrowseFilesMode(); }

void LibraryActivity::clampSelector() {
  if (files.empty()) {
    selectorIndex = 0;
  } else if (selectorIndex >= files.size()) {
    selectorIndex = files.size() - 1;
  }
}

void LibraryActivity::loadLibraryDashboard() {
  if (isBookshelfMode()) {
    if (librarySafeMode) {
      libraryView = LIBRARY_VIEW_FILES;
      rawFilesLaunch = true;
      loadFilesystemFiles();
      clampSelector();
      return;
    }
    const bool restoredSnapshot = restoreLibraryDashboardSnapshot();
    if (libraryScanRequested || !restoredSnapshot || files.empty()) {
      startLibraryIndexing();
      libraryScanRequested = false;
    } else {
      libraryIndexingActive = false;
      libraryCurrentScanFolder.clear();
      if (libraryFilterMode == LIBRARY_FILTER_ALL && SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_AUTHOR &&
          !restoreLibraryAuthorGroupSnapshot()) {
        sortLibraryDashboardBooks();
      }
    }
    return;
  }

  const char* finishedTitle = tr(STR_FINISHED_BOOKS);
  files.emplace_back(std::string("@") + finishedTitle);
  completedFileStates.push_back(0);
  progressFileStates.push_back(0);
  libraryFileStates.push_back(LIBRARY_VIEW_FINISHED);
  folderItemCounts.push_back(0);
  entryPaths.emplace_back();
  entryTitles.emplace_back(finishedTitle);
  entrySubtitles.emplace_back(I18N.get(StrId::STR_COMPLETED_READING));
  addEntryCoverPlaceholder();
  entryTypes.push_back(ENTRY_TYPE_SECTION_HEADER);
  authorGroupCoverPaths.emplace_back();

  auto root = Storage.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  std::vector<std::string> rootEntries;
  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    const bool hidden = (!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0;
    std::string_view filename{name};
    const bool visibleFile = FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                             FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                             FsHelpers::hasBmpExtension(filename);
    if (!hidden && (file.isDirectory() || visibleFile)) {
      rootEntries.emplace_back(file.isDirectory() ? std::string(name) + "/" : std::string(name));
    }
    file.close();
  }
  root.close();
  sortFileList(rootEntries);

  for (const auto& entry : rootEntries) {
    files.push_back(entry);
    completedFileStates.push_back(0);
    progressFileStates.push_back(0);
    entryRecentReadAt.push_back(0);
    libraryFileStates.push_back(LIBRARY_STATE_UNREAD);
    const bool isFolder = !entry.empty() && entry.back() == '/';
    folderItemCounts.push_back(isFolder ? countFolderItems(entry) : 0);
    entryPaths.push_back("/" + entry);
    entryTitles.push_back(getFileName(entry));
    entrySubtitles.emplace_back(isFolder ? "" : fileTypeLabel(entry));
    addEntryCoverPlaceholder();
    entryTypes.push_back(ENTRY_TYPE_BOOK);
    authorGroupCoverPaths.emplace_back();
  }
}

bool LibraryActivity::restoreLibraryDashboardSnapshot() {
  const auto& snapshot = libraryDashboardSnapshot();
  if (snapshot.valid) {
    files = snapshot.files;
    completedFileStates = snapshot.completedFileStates;
    progressFileStates = snapshot.progressFileStates;
    entryRecentReadAt = snapshot.entryRecentReadAt;
    libraryFileStates = snapshot.libraryFileStates;
    folderItemCounts = snapshot.folderItemCounts;
    entryPaths = snapshot.entryPaths;
    entryTitles = snapshot.entryTitles;
    entrySubtitles = snapshot.entrySubtitles;
    entryCoverPaths = snapshot.entryCoverPaths;
    entryCoverSourcePaths = snapshot.entryCoverSourcePaths;
    entryCoverStates = snapshot.entryCoverStates;
    entryTypes = snapshot.entryTypes;
    authorGroupCoverPaths = snapshot.authorGroupCoverPaths;
    if (entryTypes.size() != files.size()) {
      entryTypes.assign(files.size(), ENTRY_TYPE_BOOK);
    }
    if (authorGroupCoverPaths.size() != files.size()) {
      authorGroupCoverPaths.assign(files.size(), {});
    }
    if (entryRecentReadAt.size() != files.size()) {
      entryRecentReadAt.assign(files.size(), 0);
    }
    clampSelector();
    return true;
  }

  return restoreLibraryDashboardIndex();
}

void LibraryActivity::loadLibraryRenderState() {
  bool correctedLayout = forceLibraryLayout3x3();
  if (!Storage.exists(LIBRARY_RENDER_STATE_FILE)) {
    libraryFilterMode = LIBRARY_FILTER_ALL;
    librarySortDescending = SETTINGS.librarySortDescending != 0;
    libraryView = LIBRARY_VIEW_DASHBOARD;
    selectorIndex = 0;
    libraryAuthorViewKey.clear();
    libraryAuthorViewName.clear();
    librarySeriesViewName.clear();
    libraryAuthorGroupSelectorIndex = 0;
    if (correctedLayout) {
      saveLibraryRenderState();
    }
    return;
  }

  const String saved = Storage.readFile(LIBRARY_RENDER_STATE_FILE);
  int start = 0;
  while (start < saved.length()) {
    int end = saved.indexOf('\n', start);
    if (end < 0) end = saved.length();
    std::string line(saved.substring(start, end).c_str());
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t tab = line.find('\t');
    if (tab != std::string::npos) {
      const std::string key = line.substr(0, tab);
      const std::string value = unescapeIndexField(line.substr(tab + 1));
      if (key == "filter") {
        libraryFilterMode = static_cast<uint8_t>(std::clamp(std::atoi(value.c_str()), 0, 2));
      } else if (key == "sort") {
        SETTINGS.librarySort =
            static_cast<uint8_t>(std::clamp(std::atoi(value.c_str()), 0,
                                            static_cast<int>(CrossPointSettings::LIBRARY_SORT_COUNT) - 1));
      } else if (key == "direction") {
        librarySortDescending = std::atoi(value.c_str()) != 0;
        SETTINGS.librarySortDescending = librarySortDescending ? 1 : 0;
      } else if (key == "layout") {
        const int savedLayout = std::atoi(value.c_str());
        if (savedLayout != CrossPointSettings::BOOKSHELF_LAYOUT_3X3) {
          correctedLayout = true;
        }
        SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_LAYOUT_3X3;
      } else if (key == "showShelf") {
        SETTINGS.showToReadShelf = std::atoi(value.c_str()) != 0;
      } else if (key == "view") {
        const int savedView = std::atoi(value.c_str());
        libraryView = (savedView == LIBRARY_VIEW_AUTHOR || savedView == LIBRARY_VIEW_SERIES) ? savedView
                                                                                             : LIBRARY_VIEW_DASHBOARD;
      } else if (key == "selection") {
        selectorIndex = static_cast<size_t>(std::max(0, std::atoi(value.c_str())));
      } else if (key == "authorKey") {
        libraryAuthorViewKey = value;
      } else if (key == "authorName") {
        libraryAuthorViewName = value;
      } else if (key == "authorSelection") {
        libraryAuthorGroupSelectorIndex = static_cast<size_t>(std::max(0, std::atoi(value.c_str())));
      } else if (key == "seriesName") {
        librarySeriesViewName = value;
      }
    }
    start = end + 1;
  }

  if (libraryView == LIBRARY_VIEW_SERIES && librarySeriesViewName.empty()) {
    libraryView = LIBRARY_VIEW_DASHBOARD;
  } else if (libraryView == LIBRARY_VIEW_AUTHOR &&
             (libraryFilterMode != LIBRARY_FILTER_ALL ||
              SETTINGS.librarySort != CrossPointSettings::LIBRARY_SORT_AUTHOR || libraryAuthorViewKey.empty())) {
    libraryView = LIBRARY_VIEW_DASHBOARD;
    libraryAuthorViewKey.clear();
    libraryAuthorViewName.clear();
  }
  if (forceLibraryLayout3x3() || correctedLayout) {
    saveLibraryRenderState();
  }
}

void LibraryActivity::saveLibraryRenderState() const {
  if (rawFilesLaunch || basepath != "/" || !isBookshelfMode()) {
    return;
  }

  std::string body;
  const auto addLine = [&body](const char* key, const std::string& value) {
    body += key;
    body += '\t';
    body += escapeIndexField(value);
    body += '\n';
  };
  addLine("filter", std::to_string(libraryFilterMode));
  addLine("sort", std::to_string(static_cast<int>(SETTINGS.librarySort)));
  addLine("direction", librarySortDescending ? "1" : "0");
  addLine("layout", std::to_string(static_cast<int>(CrossPointSettings::BOOKSHELF_LAYOUT_3X3)));
  addLine("showShelf", SETTINGS.showToReadShelf ? "1" : "0");
  addLine("view", std::to_string(static_cast<int>(libraryView)));
  addLine("selection", std::to_string(selectorIndex));
  addLine("authorKey", libraryAuthorViewKey);
  addLine("authorName", libraryAuthorViewName);
  addLine("authorSelection", std::to_string(libraryAuthorGroupSelectorIndex));
  addLine("seriesName", librarySeriesViewName);
  Storage.writeFile(LIBRARY_RENDER_STATE_FILE, String(body.c_str()));
}

void LibraryActivity::beginLibrarySearchSession() {
  if (librarySearchSessionActive) {
    return;
  }
  librarySearchSessionActive = true;
  librarySearchPreviousFilterMode = libraryFilterMode;
  librarySearchPreviousView = libraryView;
  librarySearchPreviousSort = SETTINGS.librarySort;
  librarySearchPreviousDescending = librarySortDescending;
  librarySearchPreviousSelectorIndex = selectorIndex;
  librarySearchPreviousAuthorKey = libraryAuthorViewKey;
  librarySearchPreviousAuthorName = libraryAuthorViewName;
  librarySearchPreviousSeriesName = librarySeriesViewName;
}

void LibraryActivity::clearLibrarySearchSession(const bool restorePrevious) {
  if (librarySearchQuery.empty() && !librarySearchSessionActive) {
    return;
  }
  librarySearchQuery.clear();
  if (restorePrevious && librarySearchSessionActive) {
    libraryFilterMode = librarySearchPreviousFilterMode;
    libraryView = librarySearchPreviousView;
    SETTINGS.librarySort = librarySearchPreviousSort;
    librarySortDescending = librarySearchPreviousDescending;
    selectorIndex = librarySearchPreviousSelectorIndex;
    libraryAuthorViewKey = librarySearchPreviousAuthorKey;
    libraryAuthorViewName = librarySearchPreviousAuthorName;
    librarySeriesViewName = librarySearchPreviousSeriesName;
    loadFiles();
    clampSelector();
  }
  librarySearchSessionActive = false;
  librarySelectionOnlyRender = false;
  libraryHasRetainedGridRects = false;
  libraryRetainedRenderToken.clear();
  libraryCoverManager.cancelPrefetch();
}

bool LibraryActivity::restoreLibraryAuthorGroupSnapshot() {
  const auto& snapshot = libraryAuthorGroupSnapshot();
  if (!snapshot.valid || snapshot.files.empty()) {
    return false;
  }
  if (snapshot.entryTypes.size() != snapshot.files.size() ||
      snapshot.authorGroupCoverPaths.size() != snapshot.files.size()) {
    return false;
  }
  files = snapshot.files;
  completedFileStates = snapshot.completedFileStates;
  progressFileStates = snapshot.progressFileStates;
  entryRecentReadAt = snapshot.entryRecentReadAt;
  libraryFileStates = snapshot.libraryFileStates;
  folderItemCounts = snapshot.folderItemCounts;
  entryPaths = snapshot.entryPaths;
  entryTitles = snapshot.entryTitles;
  entrySubtitles = snapshot.entrySubtitles;
  entryCoverPaths = snapshot.entryCoverPaths;
  entryCoverSourcePaths = snapshot.entryCoverSourcePaths;
  entryCoverStates = snapshot.entryCoverStates;
  entryTypes = snapshot.entryTypes;
  authorGroupCoverPaths = snapshot.authorGroupCoverPaths;
  if (entryRecentReadAt.size() != files.size()) {
    entryRecentReadAt.assign(files.size(), 0);
  }
  clampSelector();
  return true;
}

bool LibraryActivity::isBadLibraryPath(const std::string& path) const {
  return std::find(libraryBadPaths.begin(), libraryBadPaths.end(), path) != libraryBadPaths.end();
}

void LibraryActivity::markBadLibraryPath(const std::string& path) {
  if (!path.empty() && !isBadLibraryPath(path)) {
    libraryBadPaths.push_back(path);
  }
  if (libraryFailureCount < 255) {
    ++libraryFailureCount;
  }
}

void LibraryActivity::recordSkippedLibraryBook(const std::string& path, const std::string& phase,
                                               const std::string& reason, const std::string& title) {
  if (librarySkippedBookCount < 65535) {
    ++librarySkippedBookCount;
  }
  if (libraryProgressSkippedFiles < 65535) {
    ++libraryProgressSkippedFiles;
  }
  markBadLibraryPath(path);
  LibrarySkippedBook skipped;
  skipped.path = path;
  skipped.filename = titleFromPath(path);
  skipped.title = title.empty() ? skipped.filename : title;
  skipped.phase = phase;
  skipped.reason = reason.empty() ? tr(STR_UNKNOWN_ERROR) : reason;
  if (librarySkippedBooks.size() < MAX_LIBRARY_SKIPPED_BOOK_DETAILS) {
    librarySkippedBooks.push_back(std::move(skipped));
  }
  recordLibraryBreadcrumb(phase.c_str(), basepath, path, static_cast<int>(libraryMetadataResolveIndex),
                          static_cast<int>(libraryDiscoveredBookPaths.size()));
}

void LibraryActivity::recordLibraryBreadcrumb(const char* phase, const std::string& path,
                                                  const std::string& bookPath, const int index,
                                                  const int count) const {
  if (rawFilesLaunch) {
    return;
  }
  const char* safePhase = phase != nullptr ? phase : "unknown";
  Storage.mkdir("/.crosspoint");
  std::string body = "phase=";
  body += safePhase;
  body += "\npath=" + path;
  body += "\nbook=" + bookPath;
  const int safeCount = std::max(0, count);
  const bool boundaryPhase = strcmp(safePhase, "metadata finalize") == 0 ||
                             strcmp(safePhase, "library finalize") == 0 ||
                             strcmp(safePhase, "finished") == 0;
  const int safeIndex =
      safeCount <= 0 ? -1 : std::clamp(index, 0, boundaryPhase ? safeCount : safeCount - 1);
  body += "\nindex=" + std::to_string(safeIndex);
  body += "\ncount=" + std::to_string(safeCount);
  body += "\nlayout=" + std::to_string(static_cast<int>(CrossPointSettings::BOOKSHELF_LAYOUT_3X3));
  body += "\nindexing=" + std::to_string(libraryIndexingActive ? 1 : 0);
  body += "\nthumbs=" + std::to_string(usesBookshelfGrid() ? 1 : 0);
  body += "\nfailures=" + std::to_string(static_cast<int>(libraryFailureCount));
  body += "\nheap=" + std::to_string(static_cast<unsigned long>(ESP.getFreeHeap()));
  Storage.writeFile(LIBRARY_BREADCRUMB_FILE, String(body.c_str()));
}

void LibraryActivity::clearLibraryBreadcrumb() const {
  Storage.mkdir("/.crosspoint");
  Storage.writeFile(LIBRARY_BREADCRUMB_FILE, String("phase=closed\npath=\nbook=\nindex=-1\ncount=0"));
}

bool LibraryActivity::shouldEnterLibrarySafeMode() const {
  if (rawFilesLaunch || basepath != "/" || !isBookshelfMode() || !Storage.exists(LIBRARY_BREADCRUMB_FILE)) {
    return false;
  }
  const String breadcrumb = Storage.readFile(LIBRARY_BREADCRUMB_FILE);
  if (breadcrumb.length() == 0) {
    return false;
  }
  if (breadcrumb.indexOf("phase=closed") >= 0 || breadcrumb.indexOf("phase=safe-mode") >= 0) {
    return false;
  }
  return breadcrumb.indexOf("phase=render grid") >= 0 || breadcrumb.indexOf("phase=sort") >= 0 ||
         breadcrumb.indexOf("phase=sort_validation_failed") >= 0 ||
         breadcrumb.indexOf("phase=cover_job_start") >= 0 ||
         breadcrumb.indexOf("phase=cover_job_failed") >= 0 ||
         breadcrumb.indexOf("phase=resolve metadata") >= 0 || breadcrumb.indexOf("phase=add book") >= 0;
}

void LibraryActivity::saveLibraryDashboardSnapshot() const {
  if (!isLibraryDashboard() || basepath != "/" || files.empty()) {
    return;
  }
  for (uint8_t type : entryTypes) {
    if (type != ENTRY_TYPE_BOOK) {
      return;
    }
  }

  auto& snapshot = libraryDashboardSnapshot();
  snapshot.files = files;
  snapshot.completedFileStates = completedFileStates;
  snapshot.progressFileStates = progressFileStates;
  snapshot.entryRecentReadAt = entryRecentReadAt;
  snapshot.libraryFileStates = libraryFileStates;
  snapshot.folderItemCounts = folderItemCounts;
  snapshot.entryPaths = entryPaths;
  snapshot.entryTitles = entryTitles;
  snapshot.entrySubtitles = entrySubtitles;
  snapshot.entryCoverPaths = entryCoverPaths;
  snapshot.entryCoverSourcePaths = entryCoverSourcePaths;
  snapshot.entryCoverStates = entryCoverStates;
  snapshot.entryTypes = entryTypes;
  snapshot.authorGroupCoverPaths = authorGroupCoverPaths;
  snapshot.valid = true;
}

void LibraryActivity::saveLibraryAuthorGroupSnapshot() const {
  if (!isLibraryDashboard() || basepath != "/" || files.empty() || libraryFilterMode != LIBRARY_FILTER_ALL ||
      SETTINGS.librarySort != CrossPointSettings::LIBRARY_SORT_AUTHOR) {
    return;
  }
  bool hasAuthorGroups = false;
  for (const uint8_t type : entryTypes) {
    if (type == ENTRY_TYPE_AUTHOR_GROUP) {
      hasAuthorGroups = true;
      break;
    }
  }
  if (!hasAuthorGroups) {
    return;
  }
  auto& snapshot = libraryAuthorGroupSnapshot();
  snapshot.files = files;
  snapshot.completedFileStates = completedFileStates;
  snapshot.progressFileStates = progressFileStates;
  snapshot.entryRecentReadAt = entryRecentReadAt;
  snapshot.libraryFileStates = libraryFileStates;
  snapshot.folderItemCounts = folderItemCounts;
  snapshot.entryPaths = entryPaths;
  snapshot.entryTitles = entryTitles;
  snapshot.entrySubtitles = entrySubtitles;
  snapshot.entryCoverPaths = entryCoverPaths;
  snapshot.entryCoverSourcePaths = entryCoverSourcePaths;
  snapshot.entryCoverStates = entryCoverStates;
  snapshot.entryTypes = entryTypes;
  snapshot.authorGroupCoverPaths = authorGroupCoverPaths;
  snapshot.valid = true;
}

bool LibraryActivity::restoreLibraryDashboardIndex() {
  if (!Storage.exists(LIBRARY_DASHBOARD_INDEX_FILE)) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("LIBIDX", LIBRARY_DASHBOARD_INDEX_FILE, file)) {
    return false;
  }

  std::string line;
  if (!readIndexLine(file, line) || line != "CPR_LIBRARY_DASHBOARD_V1") {
    file.close();
    return false;
  }

  while (readIndexLine(file, line) && files.size() < MAX_LIBRARY_DASHBOARD_BOOKS) {
    if (line.empty()) {
      continue;
    }
    const auto fields = splitIndexLine(line);
    if (fields.size() < 10) {
      continue;
    }
    const std::string& path = fields[1];
    if (path.empty() || pathHasIgnoredLibraryFolder(path) || !isLibraryBookCandidate(path)) {
      continue;
    }
    bool duplicate = false;
    const std::string stableBookId = BookIdentity::resolveStableBookId(path);
    for (size_t existingIndex = 0; existingIndex < entryPaths.size(); ++existingIndex) {
      if (entryPaths[existingIndex] == path) {
        duplicate = true;
        break;
      }
      if (!stableBookId.empty() && existingIndex < entryTypes.size() && entryTypes[existingIndex] == ENTRY_TYPE_BOOK &&
          BookIdentity::resolveStableBookId(entryPaths[existingIndex]) == stableBookId) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    const uint8_t completed = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[7].c_str()))));
    const uint8_t progress = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[6].c_str()))));
    const uint8_t state = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[8].c_str()))));
    uint8_t coverState = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[9].c_str()))));
    std::string coverPath = fields[4];
    if (coverState == ENTRY_COVER_READY && coverPath.empty()) {
      coverPath.clear();
      coverState = ENTRY_COVER_UNKNOWN;
    }

    files.push_back(fields[0].empty() ? path : fields[0]);
    entryPaths.push_back(path);
    entryTitles.push_back(fields[2]);
    entrySubtitles.push_back(fields[3]);
    entryCoverPaths.push_back(coverPath);
    entryCoverSourcePaths.push_back(fields[5]);
    entryCoverStates.push_back(coverState);
    entryTypes.push_back(ENTRY_TYPE_BOOK);
    authorGroupCoverPaths.emplace_back();
    completedFileStates.push_back(completed);
    progressFileStates.push_back(progress);
    entryRecentReadAt.push_back(fields.size() > 10 ? static_cast<uint32_t>(std::max(0, atoi(fields[10].c_str()))) : 0);
    libraryFileStates.push_back(state >= LIBRARY_STATE_SECTION ? LIBRARY_STATE_UNREAD : state);
    folderItemCounts.push_back(0);
  }
  file.close();
  clampSelector();
  if (!files.empty()) {
    saveLibraryDashboardSnapshot();
    return true;
  }
  return false;
}

void LibraryActivity::saveLibraryDashboardIndex() const {
  if (!isLibraryDashboard() || basepath != "/" || files.empty() || libraryFilterMode != LIBRARY_FILTER_ALL) {
    return;
  }
  for (uint8_t type : entryTypes) {
    if (type != ENTRY_TYPE_BOOK) {
      return;
    }
  }

  Storage.mkdir("/.crosspoint");
  if (Storage.exists(LIBRARY_DASHBOARD_INDEX_TMP)) {
    Storage.remove(LIBRARY_DASHBOARD_INDEX_TMP);
  }

  FsFile file;
  if (!Storage.openFileForWrite("LIBIDX", LIBRARY_DASHBOARD_INDEX_TMP, file)) {
    return;
  }
  file.print("CPR_LIBRARY_DASHBOARD_V1\n");

  const size_t rowCount = std::min({files.size(), entryPaths.size(), entryTitles.size(), entrySubtitles.size(),
                                    entryCoverPaths.size(), entryCoverSourcePaths.size(), entryCoverStates.size(),
                                    entryTypes.size(), completedFileStates.size(), progressFileStates.size(),
                                    entryRecentReadAt.size(), libraryFileStates.size()});
  for (size_t index = 0; index < rowCount; ++index) {
    const std::string& path = entryPaths[index];
    if (entryTypes[index] != ENTRY_TYPE_BOOK || path.empty() || pathHasIgnoredLibraryFolder(path) ||
        libraryFileStates[index] >= LIBRARY_STATE_SECTION ||
        !isLibraryBookCandidate(path)) {
      continue;
    }

    file.print(escapeIndexField(files[index]).c_str());
    file.print('\t');
    file.print(escapeIndexField(path).c_str());
    file.print('\t');
    file.print(escapeIndexField(entryTitles[index]).c_str());
    file.print('\t');
    file.print(escapeIndexField(entrySubtitles[index]).c_str());
    file.print('\t');
    file.print(escapeIndexField(entryCoverPaths[index]).c_str());
    file.print('\t');
    file.print(escapeIndexField(entryCoverSourcePaths[index]).c_str());
    file.print('\t');
    file.print(static_cast<int>(progressFileStates[index]));
    file.print('\t');
    file.print(static_cast<int>(completedFileStates[index]));
    file.print('\t');
    file.print(static_cast<int>(libraryFileStates[index]));
    file.print('\t');
    file.print(static_cast<int>(entryCoverStates[index]));
    file.print('\t');
    file.print(static_cast<unsigned long>(entryRecentReadAt[index]));
    file.print('\n');
  }
  file.close();

  if (Storage.exists(LIBRARY_DASHBOARD_INDEX_FILE) && !Storage.remove(LIBRARY_DASHBOARD_INDEX_FILE)) {
    Storage.remove(LIBRARY_DASHBOARD_INDEX_TMP);
    return;
  }
  Storage.rename(LIBRARY_DASHBOARD_INDEX_TMP, LIBRARY_DASHBOARD_INDEX_FILE);
}

bool LibraryActivity::restoreLibraryIndexCheckpoint() {
  if (!Storage.exists(LIBRARY_INDEX_CHECKPOINT_FILE)) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("LIBCK", LIBRARY_INDEX_CHECKPOINT_FILE, file)) {
    return false;
  }

  std::string line;
  if (!readIndexLine(file, line) || line != "CPR_LIBRARY_INDEX_CHECKPOINT_V1") {
    file.close();
    return false;
  }

  std::string stage;
  size_t metadataIndex = 0;
  std::vector<std::string> paths;
  while (readIndexLine(file, line)) {
    if (line.empty()) {
      continue;
    }
    const auto fields = splitIndexLine(line);
    if (fields.size() < 2) {
      continue;
    }
    if (fields[0] == "stage") {
      stage = fields[1];
    } else if (fields[0] == "metadataIndex") {
      metadataIndex = static_cast<size_t>(std::max(0, atoi(fields[1].c_str())));
    } else if (fields[0] == "path") {
      const std::string& path = fields[1];
      if (!path.empty() && !pathHasIgnoredLibraryFolder(path) && isLibraryBookCandidate(path)) {
        paths.push_back(path);
      }
    }
  }
  file.close();

  if (stage.empty()) {
    return false;
  }

  libraryDiscoveredBookPaths = std::move(paths);
  libraryMetadataResolveIndex = std::min(metadataIndex, libraryDiscoveredBookPaths.size());
  libraryProgressBookCount = static_cast<uint16_t>(std::min<size_t>(65535, libraryDiscoveredBookPaths.size()));
  libraryProgressStartBookCount = libraryProgressBookCount;
  libraryProgressNewBookCount = 0;
  libraryProgressUpdatedBookCount = 0;
  libraryIndexSummaryVisible = false;
  libraryIndexCanceled = false;

  if (stage == "discovery") {
    libraryIndexStage = LIBRARY_INDEX_STAGE_METADATA;
    libraryMetadataResolveIndex = 0;
    libraryIndexingActive = true;
    recordLibraryBreadcrumb("checkpoint discovery", basepath, "", 0,
                            static_cast<int>(libraryDiscoveredBookPaths.size()));
    return !libraryDiscoveredBookPaths.empty();
  }

  if (stage == "metadata") {
    libraryIndexStage = libraryMetadataResolveIndex >= libraryDiscoveredBookPaths.size()
                            ? LIBRARY_INDEX_STAGE_FINALIZE
                            : LIBRARY_INDEX_STAGE_METADATA;
    libraryIndexingActive = true;
    if (files.empty()) {
      restoreLibraryDashboardIndex();
    }
    reserveLibraryRowCapacity(files.size() + libraryDiscoveredBookPaths.size());
    recordLibraryBreadcrumb("checkpoint metadata", basepath, "", static_cast<int>(libraryMetadataResolveIndex),
                            static_cast<int>(libraryDiscoveredBookPaths.size()));
    return !libraryDiscoveredBookPaths.empty();
  }

  if (stage == "finalized" || stage == "covers" || stage == "complete") {
    libraryIndexStage = LIBRARY_INDEX_STAGE_IDLE;
    libraryIndexingActive = false;
    if (files.empty()) {
      restoreLibraryDashboardIndex();
    }
    if (stage == "covers" && !files.empty()) {
      libraryProgressAction = LIBRARY_PROGRESS_COVERS;
      libraryPostIndexCoverWarmup = true;
      libraryCoverWarmupPending = true;
      libraryCoverWarmupTotal = 0;
      libraryCoverWarmupDone = 0;
      libraryCoverWarmupQueueCount = 0;
    }
    recordLibraryBreadcrumb(stage == "complete" ? "checkpoint complete" : "checkpoint finalized", basepath, "",
                            static_cast<int>(files.size()), static_cast<int>(libraryDiscoveredBookPaths.size()));
    return !files.empty();
  }

  return false;
}

void LibraryActivity::saveLibraryIndexCheckpoint(const char* stage) const {
  if (stage == nullptr || stage[0] == '\0') {
    return;
  }
  Storage.mkdir("/.crosspoint");
  if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_TMP)) {
    Storage.remove(LIBRARY_INDEX_CHECKPOINT_TMP);
  }
  FsFile file;
  if (!Storage.openFileForWrite("LIBCK", LIBRARY_INDEX_CHECKPOINT_TMP, file)) {
    return;
  }
  file.print("CPR_LIBRARY_INDEX_CHECKPOINT_V1\n");
  file.print("stage\t");
  file.print(escapeIndexField(stage).c_str());
  file.print('\n');
  file.print("metadataIndex\t");
  file.print(static_cast<unsigned long>(libraryMetadataResolveIndex));
  file.print('\n');
  const size_t maxPaths = std::min(libraryDiscoveredBookPaths.size(), MAX_LIBRARY_DASHBOARD_BOOKS);
  for (size_t index = 0; index < maxPaths; ++index) {
    const std::string& path = libraryDiscoveredBookPaths[index];
    if (path.empty() || pathHasIgnoredLibraryFolder(path) || !isLibraryBookCandidate(path)) {
      continue;
    }
    file.print("path\t");
    file.print(escapeIndexField(path).c_str());
    file.print('\n');
  }
  file.close();
  if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_FILE) && !Storage.remove(LIBRARY_INDEX_CHECKPOINT_FILE)) {
    Storage.remove(LIBRARY_INDEX_CHECKPOINT_TMP);
    return;
  }
  Storage.rename(LIBRARY_INDEX_CHECKPOINT_TMP, LIBRARY_INDEX_CHECKPOINT_FILE);
}

void LibraryActivity::clearLibraryIndexCheckpoint() const {
  if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_TMP)) {
    Storage.remove(LIBRARY_INDEX_CHECKPOINT_TMP);
  }
  if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_FILE)) {
    Storage.remove(LIBRARY_INDEX_CHECKPOINT_FILE);
  }
}

void LibraryActivity::saveLibraryRootSignature() const {
  if (!isLibraryDashboard() || basepath != "/" || libraryFilterMode != LIBRARY_FILTER_ALL) {
    return;
  }
  const std::string signature = computeLibraryRootSignature();
  if (signature.empty()) {
    return;
  }
  Storage.mkdir("/.crosspoint");
  Storage.writeFile(LIBRARY_DASHBOARD_SIGNATURE_FILE, String(signature.c_str()));
}

void LibraryActivity::rebuildResolvedLibraryRows() {
  resolvedRows.clear();
  const size_t rowCount = std::min({files.size(), entryPaths.size(), entryTitles.size(), entrySubtitles.size(),
                                    entryCoverPaths.size(), progressFileStates.size(), libraryFileStates.size(),
                                    entryTypes.size()});
  resolvedRows.reserve(rowCount);
  for (size_t index = 0; index < rowCount; ++index) {
    ResolvedLibraryRow row;
    row.path = entryPaths[index];
    row.title = entryTitles[index].empty() ? (row.path.empty() ? getFileName(files[index]) : titleFromPath(row.path))
                                           : entryTitles[index];
    row.author = entrySubtitles[index].empty() ? authorDisplayName("", row.path) : entrySubtitles[index];
    row.coverPath = entryCoverPaths[index];
    row.progressPercent = progressFileStates[index];
    row.state = libraryFileStates[index];
    row.type = entryTypes[index];
    row.originalIndex = static_cast<uint16_t>(std::min<size_t>(65535, index));
    row.finished = row.state == LIBRARY_STATE_FINISHED;
    row.toRead = row.state == LIBRARY_STATE_TO_READ;
    row.titleSortKey = lowercaseCopy(trimCopy(row.title));
    row.authorSortKey = authorSortKey(row.author, row.path, row.title);
    row.recentSortValue = index < entryRecentReadAt.size() ? entryRecentReadAt[index] : 0;
    row.searchText = row.title + " " + row.author + " " + row.path;
    resolvedRows.push_back(std::move(row));
  }
}

void LibraryActivity::resetLibraryDashboardState(const bool clearPersistedIndex) {
  auto& snapshot = libraryDashboardSnapshot();
  snapshot = {};
  libraryAuthorGroupSnapshot() = {};
  files.clear();
  completedFileStates.clear();
  progressFileStates.clear();
  entryRecentReadAt.clear();
  libraryFileStates.clear();
  folderItemCounts.clear();
  entryPaths.clear();
  entryTitles.clear();
  entrySubtitles.clear();
  entryCoverPaths.clear();
  entryCoverSourcePaths.clear();
  entryCoverStates.clear();
  entryTypes.clear();
  authorGroupCoverPaths.clear();
  resolvedRows.clear();
  libraryCoverManager.clearWindows();
  libraryScanFolders.clear();
  libraryScanOffsets.clear();
  libraryCurrentScanFolder.clear();
  librarySkippedFolderName.clear();
  libraryBadPaths.clear();
  libraryFailureCount = 0;
  libraryIndexingActive = false;
  libraryAuthorViewKey.clear();
  libraryAuthorViewName.clear();
  if (clearPersistedIndex) {
    thumbnailCache().clear();
    if (Storage.exists(LIBRARY_DASHBOARD_INDEX_FILE)) Storage.remove(LIBRARY_DASHBOARD_INDEX_FILE);
    if (Storage.exists(LIBRARY_DASHBOARD_INDEX_TMP)) Storage.remove(LIBRARY_DASHBOARD_INDEX_TMP);
    if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_FILE)) Storage.remove(LIBRARY_INDEX_CHECKPOINT_FILE);
    if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_TMP)) Storage.remove(LIBRARY_INDEX_CHECKPOINT_TMP);
    if (Storage.exists(LIBRARY_DASHBOARD_SIGNATURE_FILE)) Storage.remove(LIBRARY_DASHBOARD_SIGNATURE_FILE);
    if (Storage.exists(LIBRARY_RENDER_STATE_FILE)) Storage.remove(LIBRARY_RENDER_STATE_FILE);
    if (Storage.exists(LIBRARY_COVER_CACHE_FILE)) Storage.remove(LIBRARY_COVER_CACHE_FILE);
  }
}

void LibraryActivity::clearGeneratedLibraryCache() {
  for (const auto& record : thumbnailCache()) {
    if (!record.thumbPath.empty() && record.thumbPath.rfind("/.crosspoint/", 0) == 0 &&
        Storage.exists(record.thumbPath.c_str())) {
      Storage.remove(record.thumbPath.c_str());
    }
  }
  thumbnailCache().clear();
  if (Storage.exists(LIBRARY_DASHBOARD_INDEX_FILE)) Storage.remove(LIBRARY_DASHBOARD_INDEX_FILE);
  if (Storage.exists(LIBRARY_DASHBOARD_INDEX_TMP)) Storage.remove(LIBRARY_DASHBOARD_INDEX_TMP);
  if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_FILE)) Storage.remove(LIBRARY_INDEX_CHECKPOINT_FILE);
  if (Storage.exists(LIBRARY_INDEX_CHECKPOINT_TMP)) Storage.remove(LIBRARY_INDEX_CHECKPOINT_TMP);
  if (Storage.exists(LIBRARY_DASHBOARD_SIGNATURE_FILE)) Storage.remove(LIBRARY_DASHBOARD_SIGNATURE_FILE);
  if (Storage.exists(LIBRARY_RENDER_STATE_FILE)) Storage.remove(LIBRARY_RENDER_STATE_FILE);
  if (Storage.exists(LIBRARY_COVER_CACHE_FILE)) Storage.remove(LIBRARY_COVER_CACHE_FILE);
}

void LibraryActivity::collectLibraryEpubPaths(const std::string& folderPath, std::vector<std::string>& paths,
                                                  const int depth) const {
  if (depth > MAX_LIBRARY_SCAN_DEPTH || paths.size() >= MAX_LIBRARY_DASHBOARD_BOOKS) {
    return;
  }
  auto dir = Storage.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  char name[500];
  for (auto file = dir.openNextFile(); file && paths.size() < MAX_LIBRARY_DASHBOARD_BOOKS; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    const bool hidden = (!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0;
    if (hidden) {
      file.close();
      continue;
    }
    std::string childPath = folderPath;
    if (childPath.empty() || childPath.back() != '/') childPath += "/";
    childPath += name;
    if (file.isDirectory()) {
      file.close();
      if (!isIgnoredLibraryFolder(name) && !pathHasIgnoredLibraryFolder(childPath)) {
        collectLibraryEpubPaths(childPath, paths, depth + 1);
      }
      continue;
    }
    std::string_view filename{name};
    if (isLibraryBookCandidate(filename) && !pathHasIgnoredLibraryFolder(childPath)) {
      paths.push_back(childPath);
    }
    file.close();
  }
  dir.close();
}

void LibraryActivity::repairStoresForLiveLibraryPaths(const std::vector<std::string>& livePaths) {
  if (livePaths.empty()) {
    return;
  }
  READING_STATS.repairRenamedBooks(livePaths);
  LIBRARY_METADATA.repairRenamedBooks(livePaths);
  RECENT_BOOKS.repairRenamedBooks(livePaths);
}

void LibraryActivity::startLibraryIndexing() {
  if (librarySafeMode || !usesBookshelfGrid() || !isLibraryDashboard()) {
    libraryIndexingActive = false;
    return;
  }
  recordLibraryBreadcrumb("start indexing", basepath, "", static_cast<int>(selectorIndex),
                          static_cast<int>(files.size()));
  libraryScanFolders.clear();
  libraryScanOffsets.clear();
  libraryDiscoveredBookPaths.clear();
  libraryMetadataResolveIndex = 0;
  libraryCurrentScanFolder.clear();
  libraryLastMetadataPath.clear();
  libraryCurrentMetadataPath.clear();
  libraryCurrentMetadataTitle.clear();
  libraryCurrentMetadataAuthor.clear();
  librarySkippedFolderName.clear();
  libraryIndexingActive = true;
  libraryIndexStage = LIBRARY_INDEX_STAGE_DISCOVERY;
  libraryProgressStartBookCount = static_cast<uint16_t>(std::min<size_t>(65535, files.size()));
  libraryProgressBookCount = libraryProgressStartBookCount;
  libraryProgressNewBookCount = 0;
  libraryProgressUpdatedBookCount = 0;
  libraryProgressFoldersScanned = 0;
  libraryProgressFilesScanned = 0;
  libraryProgressSkippedFiles = 0;
  librarySkippedBookCount = 0;
  librarySkippedBooks.clear();
  libraryLastMetadataPath.clear();
  libraryCurrentMetadataPath.clear();
  libraryCurrentMetadataTitle.clear();
  libraryCurrentMetadataAuthor.clear();
  libraryPostIndexCoverWarmup = false;
  libraryCoverWarmupPending = false;
  libraryCoverWarmupTotal = 0;
  libraryCoverWarmupDone = 0;
  libraryCurrentCoverTitle.clear();
  libraryCoverWarmupQueueCount = 0;
  libraryIndexSummaryVisible = false;
  libraryIndexCanceled = false;
  lastLibraryProgressHudMs = 0;
  if (restoreLibraryIndexCheckpoint()) {
    if (libraryIndexStage == LIBRARY_INDEX_STAGE_METADATA && files.empty()) {
      restoreLibraryDashboardIndex();
    }
    libraryScanFolders.clear();
    libraryScanOffsets.clear();
    libraryProgressHudOnlyRender = true;
    requestUpdate();
    return;
  }
  libraryScanFolders.push_back("/");
  libraryScanOffsets.push_back(0);
}

bool LibraryActivity::addLibraryBookByPath(const std::string& path) {
  if (path.empty() || pathHasIgnoredLibraryFolder(path) || !isLibraryBookCandidate(path) || isBadLibraryPath(path)) {
    return false;
  }
  if (!Storage.exists(path.c_str())) {
    recordSkippedLibraryBook(path, "file open failed", tr(STR_FILE_OPEN_FAILED));
    return false;
  }
  const std::string stableBookId = BookIdentity::resolveStableBookId(path);
  const auto* statsBook = READING_STATS.findMatchingBookForPath(path, "", "");
  const auto* libraryMetadata = LIBRARY_METADATA.findBook(!stableBookId.empty() ? stableBookId : path);
  const auto* importedMetadata = libraryIndexingActive ? nullptr : BOOK_METADATA.findBook(path, stableBookId);
  const uint8_t progress = statsBook != nullptr ? statsBook->lastProgressPercent : 0;
  const uint8_t state = classifyLibraryBookState(path, libraryMetadata, statsBook);
  LibraryResolvedBookInfo resolvedInfo =
      resolveLibraryBookInfo(path, stableBookId, libraryMetadata, importedMetadata, statsBook);

  for (size_t existingIndex = 0; existingIndex < entryPaths.size(); ++existingIndex) {
    if (existingIndex >= entryTypes.size() || entryTypes[existingIndex] != ENTRY_TYPE_BOOK) {
      continue;
    }
    const std::string& existingPath = entryPaths[existingIndex];
    const bool samePath = existingPath == path;
    const bool sameStableBook =
        !stableBookId.empty() && !existingPath.empty() && BookIdentity::resolveStableBookId(existingPath) == stableBookId;
    if (!samePath && !sameStableBook) {
      continue;
    }

    bool changed = false;
    auto updateString = [&changed](std::string& target, const std::string& value) {
      if (target != value) {
        target = value;
        changed = true;
      }
    };
    auto updateByte = [&changed](uint8_t& target, const uint8_t value) {
      if (target != value) {
        target = value;
        changed = true;
      }
    };

    updateString(entryPaths[existingIndex], path);
    updateString(files[existingIndex], path);
    if (existingIndex < entryTitles.size()) {
      updateString(entryTitles[existingIndex], resolvedInfo.title);
    }
    if (existingIndex < entrySubtitles.size()) {
      updateString(entrySubtitles[existingIndex], authorDisplayName(resolvedInfo.author, path));
    }
    if (existingIndex < entryCoverSourcePaths.size()) {
      updateString(entryCoverSourcePaths[existingIndex], resolvedInfo.coverPath);
    }
    if (existingIndex < completedFileStates.size()) {
      updateByte(completedFileStates[existingIndex], state == LIBRARY_STATE_FINISHED ? 1 : 0);
    }
    if (existingIndex < progressFileStates.size()) {
      updateByte(progressFileStates[existingIndex], progress);
    }
    if (existingIndex < entryRecentReadAt.size() && statsBook != nullptr && entryRecentReadAt[existingIndex] != statsBook->lastReadAt) {
      entryRecentReadAt[existingIndex] = statsBook->lastReadAt;
      changed = true;
    }
    if (existingIndex < libraryFileStates.size()) {
      updateByte(libraryFileStates[existingIndex], state);
    }
    return changed;
  }

  std::string title = resolvedInfo.title;
  std::string author = resolvedInfo.author;
  std::string coverPath = resolvedInfo.coverPath;
  if (title.empty()) title = titleFromPath(path);
  if (author.empty()) author = authorDisplayName("", path);
  if (title.empty()) {
    recordSkippedLibraryBook(path, "metadata fallback", tr(STR_MALFORMED_METADATA), title);
    return false;
  }
  addLibraryBook(path, title, author, coverPath, progress, state, statsBook != nullptr ? statsBook->lastReadAt : 0);

  // Metadata enrichment is optional during Library indexing. The visible row already exists,
  // so parser/cache/store failures cannot remove the book from the Library.
  recordLibraryBreadcrumb("resolve metadata", basepath, path, static_cast<int>(files.size()),
                          static_cast<int>(files.size()));
  if (!libraryIndexingActive && importedMetadata != nullptr && libraryMetadata == nullptr) {
    if (!LIBRARY_METADATA.upsertBookMetadata(path, stableBookId, *importedMetadata, importedMetadata->source, false)) {
      recordLibraryBreadcrumb("metadata upsert failed", basepath, path, static_cast<int>(files.size()),
                              static_cast<int>(files.size()));
    }
  } else if (!libraryIndexingActive && libraryMetadata == nullptr) {
    CachedBookMetadata cached;
    cached.title = title;
    cached.author = author;
    cached.source = "filename-fallback";
    if (!LIBRARY_METADATA.upsertBookMetadata(path, stableBookId, cached, "filename-fallback", false)) {
      recordLibraryBreadcrumb("metadata upsert failed", basepath, path, static_cast<int>(files.size()),
                              static_cast<int>(files.size()));
    }
  }
  return true;
}

void LibraryActivity::reserveLibraryRowCapacity(const size_t capacity) {
  files.reserve(capacity);
  completedFileStates.reserve(capacity);
  progressFileStates.reserve(capacity);
  entryRecentReadAt.reserve(capacity);
  libraryFileStates.reserve(capacity);
  folderItemCounts.reserve(capacity);
  entryPaths.reserve(capacity);
  entryTitles.reserve(capacity);
  entrySubtitles.reserve(capacity);
  entryCoverPaths.reserve(capacity);
  entryCoverSourcePaths.reserve(capacity);
  entryCoverStates.reserve(capacity);
  entryTypes.reserve(capacity);
  authorGroupCoverPaths.reserve(capacity);
}

void LibraryActivity::sortLibraryDashboardBooks() {
  if ((!isLibraryDashboard() && !isLibraryAuthorView()) || files.empty()) {
    return;
  }
  recordLibraryBreadcrumb("sort", basepath, selectorIndex < entryPaths.size() ? entryPaths[selectorIndex] : "",
                          static_cast<int>(selectorIndex),
                          static_cast<int>(files.size()));
  const auto expectedSize = files.size();
  const bool validBefore = completedFileStates.size() == expectedSize && progressFileStates.size() == expectedSize &&
                           entryRecentReadAt.size() == expectedSize && libraryFileStates.size() == expectedSize &&
                           folderItemCounts.size() == expectedSize &&
                           entryPaths.size() == expectedSize && entryTitles.size() == expectedSize &&
                           entrySubtitles.size() == expectedSize && entryCoverPaths.size() == expectedSize &&
                           entryCoverSourcePaths.size() == expectedSize && entryCoverStates.size() == expectedSize &&
                           entryTypes.size() == expectedSize && authorGroupCoverPaths.size() == expectedSize;
  if (!validBefore) {
    recordLibraryBreadcrumb("sort_validation_failed", basepath, "", static_cast<int>(selectorIndex),
                            static_cast<int>(files.size()));
    clampSelector();
    return;
  }

  struct Row {
    std::string file;
    uint8_t completed = 0;
    uint8_t progress = 0;
    uint8_t state = LIBRARY_STATE_UNREAD;
    uint16_t folderCount = 0;
    std::string path;
    std::string title;
    std::string subtitle;
    std::string coverPath;
    std::string coverSourcePath;
    uint8_t coverState = ENTRY_COVER_UNKNOWN;
    uint8_t type = ENTRY_TYPE_BOOK;
    std::vector<std::string> groupCoverPaths;
    std::string titleKey;
    std::string authorKey;
    uint32_t recent = 0;
    uint16_t originalIndex = 0;
  };

  std::vector<Row> rows;
  rows.reserve(files.size());
  recordLibraryBreadcrumb("sort prepare", basepath, "", 0, static_cast<int>(expectedSize));
  for (size_t index = 0; index < files.size(); ++index) {
    const uint8_t state = libraryFileStates[index];
    Row row;
    row.file = files[index];
    row.completed = completedFileStates[index];
    row.progress = progressFileStates[index];
    row.recent = entryRecentReadAt[index];
    row.state = state;
    row.folderCount = folderItemCounts[index];
    row.path = entryPaths[index];
    row.title = entryTitles[index];
    row.subtitle = entrySubtitles[index];
    row.coverPath = entryCoverPaths[index];
    row.coverSourcePath = entryCoverSourcePaths[index];
    row.coverState = entryCoverStates[index];
    row.type = entryTypes[index];
    row.originalIndex = static_cast<uint16_t>(std::min<size_t>(65535, index));
    if (row.type != ENTRY_TYPE_BOOK) {
      continue;
    }
    if (row.state != LIBRARY_STATE_FINISHED && pathHasFinishedFolder(row.path)) {
      row.state = LIBRARY_STATE_FINISHED;
      row.completed = 1;
    }
    const bool sectionRow = row.path.empty() || row.state >= LIBRARY_STATE_SECTION;
    if (sectionRow) {
      continue;
    } else if (libraryFilterMode == LIBRARY_FILTER_FINISHED && row.state != LIBRARY_STATE_FINISHED) {
      continue;
    } else if (libraryFilterMode == LIBRARY_FILTER_TO_READ && row.state != LIBRARY_STATE_TO_READ) {
      continue;
    } else if (libraryFilterMode == LIBRARY_FILTER_ALL && row.state == LIBRARY_STATE_FINISHED) {
      continue;
    } else {
      if (index < resolvedRows.size()) {
        const auto& resolved = resolvedRows[index];
        if (!resolved.title.empty()) row.title = resolved.title;
        if (!resolved.author.empty()) row.subtitle = resolved.author;
        row.titleKey = resolved.titleSortKey;
        row.authorKey = resolved.authorSortKey;
        row.recent = resolved.recentSortValue;
      }
      if (row.title.empty()) {
        row.title = titleFromPath(row.path);
      }
      if (row.subtitle.empty()) {
        row.subtitle = authorDisplayName("", row.path);
      }
      if (row.titleKey.empty()) row.titleKey = lowercaseCopy(trimCopy(row.title));
      if (row.authorKey.empty()) row.authorKey = authorSortKey(row.subtitle, row.path, row.title);
      rows.push_back(std::move(row));
    }
  }

  auto sortBreadcrumbDetail = [](const Row* row) {
    std::string detail = "mode=" + std::to_string(static_cast<int>(SETTINGS.librarySort)) +
                         " dir=" + std::to_string(librarySortDescending ? 1 : 0);
    if (row != nullptr) {
      detail += " title=";
      detail += row->title;
      detail += " author=";
      detail += row->subtitle;
      detail += " path=";
      detail += row->path;
    }
    return detail;
  };

  recordLibraryBreadcrumb("sort keys", basepath, rows.empty() ? "" : sortBreadcrumbDetail(&rows.front()), 0,
                          static_cast<int>(rows.size()));
  auto compareTextKey = [](const std::string& a, const std::string& b, bool descending) {
    if (a == b) return false;
    return descending ? a > b : a < b;
  };
  auto compareTitleKey = [&](const Row& a, const Row& b) {
    if (a.titleKey != b.titleKey) {
      return compareTextKey(a.titleKey, b.titleKey, librarySortDescending);
    }
    return a.originalIndex < b.originalIndex;
  };
  auto compareRows = [&](const Row& a, const Row& b) {
    if (libraryFilterMode == LIBRARY_FILTER_ALL) {
      const bool toReadA = a.state == LIBRARY_STATE_TO_READ;
      const bool toReadB = b.state == LIBRARY_STATE_TO_READ;
      if (toReadA != toReadB) return toReadA;
    }
    switch (SETTINGS.librarySort) {
      case CrossPointSettings::LIBRARY_SORT_AUTHOR:
        if (a.authorKey != b.authorKey) {
          return compareTextKey(a.authorKey, b.authorKey, librarySortDescending);
        }
        return compareTitleKey(a, b);
      case CrossPointSettings::LIBRARY_SORT_RECENT:
        if (a.recent != b.recent) return librarySortDescending ? a.recent < b.recent : a.recent > b.recent;
        return compareTitleKey(a, b);
      case CrossPointSettings::LIBRARY_SORT_PROGRESS:
        if (a.progress != b.progress) return librarySortDescending ? a.progress < b.progress : a.progress > b.progress;
        return compareTitleKey(a, b);
      case CrossPointSettings::LIBRARY_SORT_TITLE:
      default:
        return compareTitleKey(a, b);
    }
  };
  if (rows.size() >= 2) {
    recordLibraryBreadcrumb("sort compare", basepath,
                            sortBreadcrumbDetail(&rows[0]) + " | " + sortBreadcrumbDetail(&rows[1]), 0,
                            static_cast<int>(rows.size()));
  }
  std::sort(rows.begin(), rows.end(), compareRows);

  if (isLibraryDashboard() && libraryFilterMode == LIBRARY_FILTER_ALL &&
      SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_AUTHOR) {
    recordLibraryBreadcrumb("build authors prepare", basepath, rows.empty() ? "" : sortBreadcrumbDetail(&rows.front()),
                            0, static_cast<int>(rows.size()));
    std::vector<Row> derivedRows;
    derivedRows.reserve(rows.size());
    struct AuthorAccumulator {
      std::string key;
      std::string name;
      uint16_t count = 0;
      std::array<std::string, 4> coverPaths;
      uint8_t coverCount = 0;
    };
    AuthorAccumulator currentGroup;
    auto flushAuthorGroup = [&derivedRows, &currentGroup]() {
      if (currentGroup.count == 0) return;
      Row groupRow;
      groupRow.file = currentGroup.name;
      groupRow.state = LIBRARY_STATE_SECTION;
      groupRow.path = currentGroup.key;
      groupRow.title = currentGroup.name;
      groupRow.subtitle = std::to_string(currentGroup.count) + " " +
                          I18N.get(currentGroup.count == 1 ? StrId::STR_BOOK : StrId::STR_BOOKS);
      groupRow.type = ENTRY_TYPE_AUTHOR_GROUP;
      groupRow.folderCount = currentGroup.count;
      for (uint8_t coverIndex = 0; coverIndex < currentGroup.coverCount; ++coverIndex) {
        groupRow.groupCoverPaths.push_back(currentGroup.coverPaths[coverIndex]);
      }
      derivedRows.push_back(std::move(groupRow));
      currentGroup = AuthorAccumulator{};
    };
    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
      const Row& row = rows[rowIndex];
      recordLibraryBreadcrumb("build authors rows", basepath, sortBreadcrumbDetail(&row),
                              static_cast<int>(rowIndex), static_cast<int>(rows.size()));
      if (row.state == LIBRARY_STATE_TO_READ) {
        derivedRows.push_back(row);
        continue;
      }
      std::string key = row.authorKey.empty() ? authorSortKey(row.subtitle, row.path, row.title) : row.authorKey;
      std::string name = authorDisplayName(row.subtitle, row.path);
      if (trimCopy(name).empty() || trimCopy(key).empty()) {
        name = tr(STR_UNKNOWN_AUTHOR);
        key = canonicalAuthorSortKey(name);
      }
      if (currentGroup.count > 0 && currentGroup.key != key) {
        flushAuthorGroup();
      }
      if (currentGroup.count == 0) {
        currentGroup.key = key;
        currentGroup.name = name;
      }
      if (currentGroup.count < 65535) {
        ++currentGroup.count;
      }
      if (currentGroup.coverCount < currentGroup.coverPaths.size() && !row.coverPath.empty()) {
        currentGroup.coverPaths[currentGroup.coverCount++] = row.coverPath;
      }
    }
    recordLibraryBreadcrumb("build authors sort", basepath, currentGroup.name, 0, static_cast<int>(rows.size()));
    flushAuthorGroup();
    rows = std::move(derivedRows);
    recordLibraryBreadcrumb("build authors done", basepath, rows.empty() ? "" : sortBreadcrumbDetail(&rows.front()), 0,
                            static_cast<int>(rows.size()));
  }

  std::vector<std::string> sortedFiles;
  std::vector<uint8_t> sortedCompleted;
  std::vector<uint8_t> sortedProgress;
  std::vector<uint32_t> sortedRecent;
  std::vector<uint8_t> sortedStates;
  std::vector<uint16_t> sortedFolderCounts;
  std::vector<std::string> sortedPaths;
  std::vector<std::string> sortedTitles;
  std::vector<std::string> sortedSubtitles;
  std::vector<std::string> sortedCoverPaths;
  std::vector<std::string> sortedCoverSourcePaths;
  std::vector<uint8_t> sortedCoverStates;
  std::vector<uint8_t> sortedEntryTypes;
  std::vector<std::vector<std::string>> sortedAuthorGroupCoverPaths;
  recordLibraryBreadcrumb("sort apply", basepath, rows.empty() ? "" : sortBreadcrumbDetail(&rows.front()), 0,
                          static_cast<int>(rows.size()));
  sortedFiles.reserve(rows.size());
  sortedCompleted.reserve(rows.size());
  sortedProgress.reserve(rows.size());
  sortedRecent.reserve(rows.size());
  sortedStates.reserve(rows.size());
  sortedFolderCounts.reserve(rows.size());
  sortedPaths.reserve(rows.size());
  sortedTitles.reserve(rows.size());
  sortedSubtitles.reserve(rows.size());
  sortedCoverPaths.reserve(rows.size());
  sortedCoverSourcePaths.reserve(rows.size());
  sortedCoverStates.reserve(rows.size());
  sortedEntryTypes.reserve(rows.size());
  sortedAuthorGroupCoverPaths.reserve(rows.size());

  for (auto& row : rows) {
    sortedFiles.push_back(std::move(row.file));
    sortedCompleted.push_back(row.completed);
    sortedProgress.push_back(row.progress);
    sortedRecent.push_back(row.recent);
    sortedStates.push_back(row.state);
    sortedFolderCounts.push_back(row.folderCount);
    sortedPaths.push_back(std::move(row.path));
    sortedTitles.push_back(std::move(row.title));
    sortedSubtitles.push_back(std::move(row.subtitle));
    sortedCoverPaths.push_back(std::move(row.coverPath));
    sortedCoverSourcePaths.push_back(std::move(row.coverSourcePath));
    sortedCoverStates.push_back(row.coverState);
    sortedEntryTypes.push_back(row.type);
    sortedAuthorGroupCoverPaths.push_back(std::move(row.groupCoverPaths));
  }

  const auto sortedSize = sortedFiles.size();
  const bool validAfter = sortedCompleted.size() == sortedSize && sortedProgress.size() == sortedSize &&
                          sortedRecent.size() == sortedSize && sortedStates.size() == sortedSize &&
                          sortedFolderCounts.size() == sortedSize &&
                          sortedPaths.size() == sortedSize && sortedTitles.size() == sortedSize &&
                          sortedSubtitles.size() == sortedSize && sortedCoverPaths.size() == sortedSize &&
                          sortedCoverSourcePaths.size() == sortedSize && sortedCoverStates.size() == sortedSize &&
                          sortedEntryTypes.size() == sortedSize && sortedAuthorGroupCoverPaths.size() == sortedSize;
  if (!validAfter) {
    recordLibraryBreadcrumb("sort_validation_failed", basepath, "", static_cast<int>(selectorIndex),
                            static_cast<int>(files.size()));
    clampSelector();
    return;
  }

  files = std::move(sortedFiles);
  completedFileStates = std::move(sortedCompleted);
  progressFileStates = std::move(sortedProgress);
  entryRecentReadAt = std::move(sortedRecent);
  libraryFileStates = std::move(sortedStates);
  folderItemCounts = std::move(sortedFolderCounts);
  entryPaths = std::move(sortedPaths);
  entryTitles = std::move(sortedTitles);
  entrySubtitles = std::move(sortedSubtitles);
  entryCoverPaths = std::move(sortedCoverPaths);
  entryCoverSourcePaths = std::move(sortedCoverSourcePaths);
  entryCoverStates = std::move(sortedCoverStates);
  entryTypes = std::move(sortedEntryTypes);
  authorGroupCoverPaths = std::move(sortedAuthorGroupCoverPaths);
  clampSelector();
  rebuildResolvedLibraryRows();
}

bool LibraryActivity::processLibraryIndexJob() {
  const bool progressWorkActive = libraryProgressAction == LIBRARY_PROGRESS_REFRESH ||
                                  libraryProgressAction == LIBRARY_PROGRESS_REBUILD;
  if (!libraryIndexingActive || librarySafeMode || !usesBookshelfGrid() || !isLibraryDashboard() ||
      mappedInput.isAnyMappedButtonPressed() || libraryRendering || (libraryWorkPaused && !progressWorkActive)) {
    return false;
  }
  const unsigned long now = millis();
  if (!libraryFirstRenderDone || now - lastLibraryRenderFinishedMs < LIBRARY_POST_RENDER_WORK_IDLE_MS ||
      now - libraryEnteredAtMs < 250 ||
      now - lastNavigationInputMs < LIBRARY_BACKGROUND_IDLE_MS || now - lastLibraryWorkMs < 120) {
    return false;
  }
  lastLibraryWorkMs = now;

  if (libraryIndexStage == LIBRARY_INDEX_STAGE_DISCOVERY) {
    if (libraryScanFolders.empty() ||
        libraryDiscoveredBookPaths.size() >= MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT) {
      repairStoresForLiveLibraryPaths(libraryDiscoveredBookPaths);
      saveLibraryIndexCheckpoint("discovery");
      libraryIndexStage = LIBRARY_INDEX_STAGE_METADATA;
      libraryMetadataResolveIndex = 0;
      libraryProgressBookCount = static_cast<uint16_t>(std::min<size_t>(65535, libraryDiscoveredBookPaths.size()));
      libraryProgressNewBookCount = 0;
      libraryProgressUpdatedBookCount = 0;
      libraryCurrentMetadataPath.clear();
      libraryCurrentMetadataTitle.clear();
      libraryCurrentMetadataAuthor.clear();
      reserveLibraryRowCapacity(files.size() + libraryDiscoveredBookPaths.size());
      libraryProgressHudOnlyRender = true;
      requestUpdate();
      return true;
    }

    const std::string folderPath = libraryScanFolders.back();
    const size_t startOffset = libraryScanOffsets.empty() ? 0 : libraryScanOffsets.back();
    libraryCurrentScanFolder = folderPath;
    libraryScanFolders.pop_back();
    if (!libraryScanOffsets.empty()) {
      libraryScanOffsets.pop_back();
    }
    const std::string discoveryDetail = "folders=" + std::to_string(libraryProgressFoldersScanned) +
                                        " files=" + std::to_string(libraryProgressFilesScanned) +
                                        " books=" + std::to_string(libraryDiscoveredBookPaths.size());
    recordLibraryBreadcrumb("discover folder", folderPath, discoveryDetail, 0,
                            static_cast<int>(libraryDiscoveredBookPaths.size()));
    const int depth = static_cast<int>(std::count(folderPath.begin(), folderPath.end(), '/'));
    if (depth > MAX_LIBRARY_SCAN_DEPTH) {
      ++libraryProgressSkippedFiles;
      return false;
    }

    auto dir = Storage.open(folderPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      markBadLibraryPath(folderPath);
      ++libraryProgressSkippedFiles;
      return false;
    }

    int filesProcessed = 0;
    int foldersQueued = 0;
    size_t entryOffset = 0;
    char name[500];
    for (auto file = dir.openNextFile();
         file && libraryDiscoveredBookPaths.size() < MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT;
         file = dir.openNextFile()) {
      if (entryOffset++ < startOffset) {
        file.close();
        continue;
      }
      if (filesProcessed >= MAX_LIBRARY_FILES_PER_TICK || foldersQueued >= MAX_LIBRARY_FOLDERS_PER_TICK) {
        libraryScanFolders.push_back(folderPath);
        libraryScanOffsets.push_back(entryOffset - 1);
        file.close();
        break;
      }
      file.getName(name, sizeof(name));
      const bool hidden =
          (!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0;
      if (hidden) {
        ++libraryProgressSkippedFiles;
        file.close();
        continue;
      }

      std::string childPath = folderPath;
      if (childPath.empty() || childPath.back() != '/') childPath += "/";
      childPath += name;

      if (file.isDirectory()) {
        file.close();
        if (isIgnoredLibraryFolder(name) || pathHasIgnoredLibraryFolder(childPath)) {
          if (librarySkippedFolderName.empty()) {
            librarySkippedFolderName = getFileName(name);
          }
          continue;
        }
        if (isBadLibraryPath(childPath)) {
          ++libraryProgressSkippedFiles;
          continue;
        }
        if (foldersQueued < MAX_LIBRARY_FOLDERS_PER_TICK) {
          libraryScanFolders.push_back(folderPath);
          libraryScanOffsets.push_back(entryOffset);
          libraryScanFolders.push_back(childPath);
          libraryScanOffsets.push_back(0);
          ++foldersQueued;
        }
        break;
      }

      ++libraryProgressFilesScanned;
      std::string_view filename{name};
      if (isLibraryBookCandidate(filename) && !isBadLibraryPath(childPath) && !pathHasIgnoredLibraryFolder(childPath)) {
        libraryDiscoveredBookPaths.push_back(childPath);
        libraryProgressBookCount = static_cast<uint16_t>(std::min<size_t>(65535, libraryDiscoveredBookPaths.size()));
      } else {
        ++libraryProgressSkippedFiles;
      }
      ++filesProcessed;
      file.close();
      if (filesProcessed >= MAX_LIBRARY_FILES_PER_TICK || foldersQueued >= MAX_LIBRARY_FOLDERS_PER_TICK) {
        libraryScanFolders.push_back(folderPath);
        libraryScanOffsets.push_back(entryOffset);
        break;
      }
    }
    dir.close();
    ++libraryProgressFoldersScanned;
    if (millis() - lastLibraryProgressHudMs > 350) {
      libraryProgressHudOnlyRender = true;
      lastLibraryProgressHudMs = millis();
      requestUpdate();
    }
    return true;
  }

  if (libraryIndexStage == LIBRARY_INDEX_STAGE_METADATA) {
    const size_t total = libraryDiscoveredBookPaths.size();
    if (libraryMetadataResolveIndex >= total) {
      recordLibraryBreadcrumb("metadata finalize", basepath, libraryLastMetadataPath,
                              static_cast<int>(libraryMetadataResolveIndex), static_cast<int>(total));
      libraryIndexStage = LIBRARY_INDEX_STAGE_FINALIZE;
    } else if (files.size() >= MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT) {
      libraryMetadataResolveIndex = total;
      recordLibraryBreadcrumb("metadata finalize", basepath, libraryLastMetadataPath,
                              static_cast<int>(libraryMetadataResolveIndex), static_cast<int>(total));
      libraryIndexStage = LIBRARY_INDEX_STAGE_FINALIZE;
    }

    bool changed = false;
    constexpr int METADATA_PER_TICK = 1;
    int resolvedThisTick = 0;
    while (libraryMetadataResolveIndex < total && resolvedThisTick < METADATA_PER_TICK &&
           files.size() < MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT) {
      const size_t candidateIndex = libraryMetadataResolveIndex;
      const std::string childPath = libraryDiscoveredBookPaths[candidateIndex];
      libraryLastMetadataPath = childPath;
      libraryCurrentMetadataPath = childPath;
      libraryCurrentMetadataTitle = titleFromPath(childPath);
      libraryCurrentMetadataAuthor = authorDisplayName("", childPath);
      recordLibraryBreadcrumb("resolve metadata", basepath, childPath, static_cast<int>(libraryMetadataResolveIndex),
                              static_cast<int>(total));
      const size_t before = files.size();
      const uint16_t skippedBefore = librarySkippedBookCount;
      if (!childPath.empty() && !isBadLibraryPath(childPath)) {
        changed = addLibraryBookByPath(childPath) || changed;
      } else {
        recordSkippedLibraryBook(childPath, "metadata skipped", tr(STR_UNKNOWN_ERROR));
      }
      if (files.size() > before) {
        ++libraryProgressNewBookCount;
      } else if (librarySkippedBookCount == skippedBefore) {
        ++libraryProgressUpdatedBookCount;
      }
      ++libraryMetadataResolveIndex;
      ++resolvedThisTick;
      libraryProgressBookCount = static_cast<uint16_t>(std::min<size_t>(65535, files.size()));
      saveLibraryDashboardIndex();
      saveLibraryIndexCheckpoint("metadata");
      delay(0);
      if (libraryMetadataResolveIndex >= total) {
        recordLibraryBreadcrumb("metadata finalize", basepath, libraryLastMetadataPath,
                                static_cast<int>(libraryMetadataResolveIndex), static_cast<int>(total));
        libraryIndexStage = LIBRARY_INDEX_STAGE_FINALIZE;
        libraryCurrentMetadataPath.clear();
        libraryCurrentMetadataTitle.clear();
        libraryCurrentMetadataAuthor.clear();
        break;
      }
    }
    if (libraryIndexStage != LIBRARY_INDEX_STAGE_FINALIZE) {
      clampSelector();
      if (millis() - lastLibraryProgressHudMs > 250 || changed) {
        libraryProgressHudOnlyRender = true;
        lastLibraryProgressHudMs = millis();
        requestUpdate();
      }
      return true;
    }
    clampSelector();
  }

  if (libraryIndexStage == LIBRARY_INDEX_STAGE_FINALIZE) {
    libraryCurrentMetadataPath.clear();
    libraryCurrentMetadataTitle.clear();
    libraryCurrentMetadataAuthor.clear();
    recordLibraryBreadcrumb("library finalize", basepath, libraryLastMetadataPath,
                            static_cast<int>(libraryMetadataResolveIndex),
                            static_cast<int>(libraryDiscoveredBookPaths.size()));
    libraryIndexingActive = false;
    libraryIndexStage = LIBRARY_INDEX_STAGE_IDLE;
    const bool shouldWarmVisibleCovers =
        libraryProgressAction == LIBRARY_PROGRESS_REFRESH || libraryProgressAction == LIBRARY_PROGRESS_REBUILD ||
        libraryProgressAction == LIBRARY_PROGRESS_NONE;
    recordLibraryBreadcrumb("save dashboard", basepath, libraryLastMetadataPath, static_cast<int>(files.size()),
                            static_cast<int>(files.size()));
    saveLibraryDashboardIndex();
    saveLibraryIndexCheckpoint("finalized");
    recordLibraryBreadcrumb("sort prepare", basepath, libraryLastMetadataPath,
                            static_cast<int>(libraryMetadataResolveIndex),
                            static_cast<int>(libraryDiscoveredBookPaths.size()));
    sortLibraryDashboardBooks();
    recordLibraryBreadcrumb("save metadata", basepath, libraryLastMetadataPath, static_cast<int>(files.size()),
                            static_cast<int>(files.size()));
    LIBRARY_METADATA.saveToFile();
    recordLibraryBreadcrumb("save dashboard", basepath, libraryLastMetadataPath, static_cast<int>(files.size()),
                            static_cast<int>(files.size()));
    if (libraryFilterMode == LIBRARY_FILTER_ALL && SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_AUTHOR) {
      saveLibraryAuthorGroupSnapshot();
    } else {
      saveLibraryDashboardSnapshot();
      saveLibraryDashboardIndex();
    }
    saveLibraryRootSignature();
    clampSelector();
    libraryDiscoveredBookPaths.clear();
    libraryMetadataResolveIndex = 0;
    recordLibraryBreadcrumb("finished", basepath, libraryLastMetadataPath, static_cast<int>(files.size()),
                            static_cast<int>(files.size()));
    libraryLastMetadataPath.clear();
    libraryIndexCanceled = false;
    libraryIndexSummaryVisible = false;
    if (shouldWarmVisibleCovers && !files.empty()) {
      libraryProgressAction = LIBRARY_PROGRESS_COVERS;
      libraryPostIndexCoverWarmup = true;
      libraryCoverWarmupPending = true;
      libraryCoverWarmupTotal = 0;
      libraryCoverWarmupDone = 0;
      libraryCurrentCoverTitle.clear();
      libraryCoverWarmupQueueCount = 0;
      libraryHotCoverWarmupStartMs = 0;
      libraryProgressHudOnlyRender = false;
      saveLibraryIndexCheckpoint("covers");
    } else {
      libraryProgressAction = LIBRARY_PROGRESS_NONE;
      libraryPostIndexCoverWarmup = false;
      libraryCoverWarmupPending = false;
      libraryCoverWarmupTotal = 0;
      libraryCoverWarmupDone = 0;
      libraryCurrentCoverTitle.clear();
      libraryCoverWarmupQueueCount = 0;
      libraryCoverManager.cancelPrefetch();
      saveLibraryIndexCheckpoint("complete");
      clearLibraryIndexCheckpoint();
    }
    recordLibraryBreadcrumb("final redraw", basepath, "", static_cast<int>(files.size()),
                            static_cast<int>(files.size()));
    requestUpdate(true);
    return false;
  }

  return false;
}

void LibraryActivity::addLibraryBook(const std::string& path, const std::string& title, const std::string& author,
                                     const std::string& coverPath, const uint8_t progress, const uint8_t state,
                                     const uint32_t recent) {
  if (path.empty()) return;
  const std::string stableBookId = BookIdentity::resolveStableBookId(path);
  for (const auto& existingPath : entryPaths) {
    if (existingPath == path) return;
    if (!stableBookId.empty() && BookIdentity::resolveStableBookId(existingPath) == stableBookId) return;
  }

  files.push_back(path);
  entryPaths.push_back(path);
  addEntryCoverPlaceholder();
  entryCoverSourcePaths.back() = coverPath;
  const size_t slash = path.find_last_of('/');
  const std::string fallbackName = slash == std::string::npos ? path : path.substr(slash + 1);
  entryTitles.push_back(title.empty() ? getFileName(fallbackName) : title);
  entrySubtitles.push_back(authorDisplayName(author, path));
  entryTypes.push_back(ENTRY_TYPE_BOOK);
  authorGroupCoverPaths.emplace_back();
  completedFileStates.push_back(state == LIBRARY_STATE_FINISHED ? 1 : 0);
  progressFileStates.push_back(progress);
  entryRecentReadAt.push_back(recent);
  libraryFileStates.push_back(state);
  folderItemCounts.push_back(0);
}

void LibraryActivity::loadLibraryShelf(const uint8_t shelf) {
  if (shelf == LIBRARY_VIEW_SERIES) {
    const std::string seriesKey = lowercaseCopy(trimCopy(librarySeriesViewName));
    if (seriesKey.empty()) return;
    LibraryManualSeriesStore store;
    store.load();
    const auto books = store.booksForSeries(librarySeriesViewName);
    if (!libraryDashboardSnapshot().valid && !restoreLibraryDashboardIndex()) {
      return;
    }
    const auto sourcePaths = libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entryPaths : entryPaths;
    const auto sourceTitles = libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entryTitles : entryTitles;
    const auto sourceSubtitles =
        libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entrySubtitles : entrySubtitles;
    const auto sourceCoverSources =
        libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entryCoverSourcePaths : entryCoverSourcePaths;
    const auto sourceCoverPaths =
        libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entryCoverPaths : entryCoverPaths;
    const auto sourceCoverStates =
        libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entryCoverStates : entryCoverStates;
    const auto sourceProgress =
        libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().progressFileStates : progressFileStates;
    const auto sourceRecent =
        libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entryRecentReadAt : entryRecentReadAt;
    const auto sourceStates =
        libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().libraryFileStates : libraryFileStates;
    files.clear();
    completedFileStates.clear();
    progressFileStates.clear();
    entryRecentReadAt.clear();
    libraryFileStates.clear();
    folderItemCounts.clear();
    entryPaths.clear();
    entryTitles.clear();
    entrySubtitles.clear();
    entryCoverPaths.clear();
    entryCoverSourcePaths.clear();
    entryCoverStates.clear();
    entryTypes.clear();
    authorGroupCoverPaths.clear();

    std::set<std::string> addedBookKeys;
    auto addSourceIndex = [&](const size_t index) {
      if (index >= sourcePaths.size() || index >= sourceTitles.size() || index >= sourceSubtitles.size() ||
          index >= sourceCoverSources.size() || index >= sourceProgress.size() || index >= sourceStates.size()) {
        return false;
      }
      const std::string stableId = BookIdentity::resolveStableBookId(sourcePaths[index]);
      const std::string addKey = stableId.empty() ? sourcePaths[index] : stableId;
      if (addKey.empty() || addedBookKeys.count(addKey) > 0) {
        return false;
      }
      addLibraryBook(sourcePaths[index], sourceTitles[index], sourceSubtitles[index], sourceCoverSources[index],
                     sourceProgress[index], sourceStates[index],
                     index < sourceRecent.size() ? sourceRecent[index] : 0);
      if (!entryCoverPaths.empty() && index < sourceCoverPaths.size() && index < sourceCoverStates.size()) {
        entryCoverPaths.back() = sourceCoverPaths[index];
        entryCoverStates.back() = sourceCoverStates[index];
      }
      addedBookKeys.insert(addKey);
      return true;
    };

    for (const auto& seriesBook : books) {
      for (size_t index = 0; index < sourcePaths.size(); ++index) {
        const bool stableMatch = !seriesBook.stableBookId.empty() &&
                                 BookIdentity::resolveStableBookId(sourcePaths[index]) == seriesBook.stableBookId;
        const bool pathMatch = !seriesBook.path.empty() && sourcePaths[index] == seriesBook.path;
        if ((stableMatch || pathMatch) && addSourceIndex(index)) break;
      }
    }
    for (size_t index = 0; index < sourcePaths.size(); ++index) {
      const std::string stableId = BookIdentity::resolveStableBookId(sourcePaths[index]);
      const auto* metadata = BOOK_METADATA.findBook(sourcePaths[index], stableId);
      if (!metadata || metadata->series.empty()) {
        continue;
      }
      if (lowercaseCopy(trimCopy(metadata->series)) == seriesKey) {
        addSourceIndex(index);
      }
    }
    return;
  }

  if (shelf == LIBRARY_VIEW_AUTHOR) {
    const auto& snapshot = libraryDashboardSnapshot();
    if (!snapshot.valid) {
      if (!restoreLibraryDashboardIndex()) {
        return;
      }
      const auto sourcePaths = entryPaths;
      const auto sourceTitles = entryTitles;
      const auto sourceSubtitles = entrySubtitles;
      const auto sourceCoverPaths = entryCoverPaths;
      const auto sourceCoverSourcePaths = entryCoverSourcePaths;
      const auto sourceCoverStates = entryCoverStates;
      const auto sourceProgress = progressFileStates;
      const auto sourceRecent = entryRecentReadAt;
      const auto sourceStates = libraryFileStates;
      files.clear();
      completedFileStates.clear();
      progressFileStates.clear();
      entryRecentReadAt.clear();
      libraryFileStates.clear();
      folderItemCounts.clear();
      entryPaths.clear();
      entryTitles.clear();
      entrySubtitles.clear();
      entryCoverPaths.clear();
      entryCoverSourcePaths.clear();
      entryCoverStates.clear();
      entryTypes.clear();
      authorGroupCoverPaths.clear();
      for (size_t index = 0; index < sourcePaths.size(); ++index) {
        if (index >= sourceTitles.size() || index >= sourceSubtitles.size() || index >= sourceCoverSourcePaths.size() ||
            index >= sourceProgress.size() || index >= sourceStates.size()) {
          continue;
        }
        const std::string title = sourceTitles[index].empty() ? titleFromPath(sourcePaths[index]) : sourceTitles[index];
        if (authorSortKey(sourceSubtitles[index], sourcePaths[index], title) != libraryAuthorViewKey) {
          continue;
        }
        addLibraryBook(sourcePaths[index], sourceTitles[index], sourceSubtitles[index], sourceCoverSourcePaths[index],
                       sourceProgress[index], sourceStates[index],
                       index < sourceRecent.size() ? sourceRecent[index] : 0);
        if (!entryCoverPaths.empty() && index < sourceCoverPaths.size() && index < sourceCoverStates.size()) {
          entryCoverPaths.back() = sourceCoverPaths[index];
          entryCoverStates.back() = sourceCoverStates[index];
        }
      }
      sortLibraryDashboardBooks();
      return;
    }
    for (size_t index = 0; index < snapshot.entryPaths.size(); ++index) {
      if (index >= snapshot.entryTitles.size() || index >= snapshot.entrySubtitles.size() ||
          index >= snapshot.entryCoverSourcePaths.size() || index >= snapshot.progressFileStates.size() ||
          index >= snapshot.libraryFileStates.size()) {
        continue;
      }
      const std::string title = snapshot.entryTitles[index].empty() ? titleFromPath(snapshot.entryPaths[index])
                                                                    : snapshot.entryTitles[index];
      if (authorSortKey(snapshot.entrySubtitles[index], snapshot.entryPaths[index], title) != libraryAuthorViewKey) {
        continue;
      }
      uint8_t state = snapshot.libraryFileStates[index];
      if (state == LIBRARY_STATE_FINISHED && libraryFilterMode != LIBRARY_FILTER_FINISHED) {
        continue;
      }
      addLibraryBook(snapshot.entryPaths[index], snapshot.entryTitles[index], snapshot.entrySubtitles[index],
                     snapshot.entryCoverSourcePaths[index], snapshot.progressFileStates[index], state,
                     index < snapshot.entryRecentReadAt.size() ? snapshot.entryRecentReadAt[index] : 0);
      if (!entryCoverPaths.empty() && index < snapshot.entryCoverPaths.size() &&
          index < snapshot.entryCoverStates.size()) {
        entryCoverPaths.back() = snapshot.entryCoverPaths[index];
        entryCoverStates.back() = snapshot.entryCoverStates[index];
      }
    }
    sortLibraryDashboardBooks();
    return;
  }

  for (const auto& book : READING_STATS.getBooks()) {
    if (book.path.empty()) continue;
    const auto* metadata = LIBRARY_METADATA.findBook(book.path);
    if (metadata != nullptr && metadata->toRead && book.lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT) {
      LIBRARY_METADATA.removeFromToRead(book.path);
      metadata = LIBRARY_METADATA.findBook(book.path);
    }
    const bool activeRemoved = metadata != nullptr && metadata->activeRemoved;
    const uint8_t state = classifyLibraryBookState(book.path, metadata, &book);
    const bool finished = state == LIBRARY_STATE_FINISHED;
    const bool toRead = state == LIBRARY_STATE_TO_READ;

    const bool include =
        (shelf == LIBRARY_VIEW_CONTINUE && !finished && !toRead && !activeRemoved &&
         book.lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT) ||
        (shelf == LIBRARY_VIEW_FINISHED && finished) ||
        (shelf == LIBRARY_VIEW_TO_READ && toRead);
    if (include) {
      addLibraryBook(book.path, book.title, book.author, book.coverBmpPath, book.lastProgressPercent, state,
                     book.lastReadAt);
    }
  }

  if (shelf == LIBRARY_VIEW_TO_READ || shelf == LIBRARY_VIEW_FINISHED) {
    for (const auto& book : LIBRARY_METADATA.getBooks()) {
      const uint8_t state = classifyLibraryBookState(book.path, &book, nullptr);
      const bool include = (shelf == LIBRARY_VIEW_TO_READ && state == LIBRARY_STATE_TO_READ) ||
                           (shelf == LIBRARY_VIEW_FINISHED && state == LIBRARY_STATE_FINISHED);
      if (include && READING_STATS.findBook(book.path) == nullptr) {
        addLibraryBook(book.path, "", "", "", 0, state);
      }
    }
  }
}

void LibraryActivity::addEntryCoverPlaceholder() {
  entryCoverPaths.emplace_back();
  entryCoverSourcePaths.emplace_back();
  entryCoverStates.push_back(ENTRY_COVER_UNKNOWN);
}

bool LibraryActivity::entryCanResolveCover(const int index) const {
  if (!usesBookshelfGrid()) {
    return false;
  }
  if (index < 0 || index >= static_cast<int>(files.size()) || index >= static_cast<int>(entryPaths.size())) {
    return false;
  }
  if (index >= static_cast<int>(entryTypes.size()) || entryTypes[index] != ENTRY_TYPE_BOOK) {
    return false;
  }
  const std::string& entry = files[index];
  if (entry.empty() || entry.back() == '/') {
    return false;
  }
  if (!isLibraryShelf() && basepath == "/" && !isLibraryDashboard()) {
    return false;
  }

  const std::string& path = entryPaths[index];
  if (pathHasIgnoredLibraryFolder(path)) {
    return false;
  }
  if (isLibraryDashboard()) {
    return FsHelpers::hasEpubExtension(path);
  }
  return FsHelpers::hasEpubExtension(path);
}

bool LibraryActivity::resolveEntryCover(const int index, const bool allowGeneration) {
  if (index >= 0 && index < static_cast<int>(entryPaths.size()) &&
      index < static_cast<int>(entryCoverStates.size()) && pathHasIgnoredLibraryFolder(entryPaths[index])) {
    entryCoverStates[index] = ENTRY_COVER_MISSING;
    return false;
  }
  if (!entryCanResolveCover(index) || index >= static_cast<int>(entryCoverPaths.size()) ||
      index >= static_cast<int>(entryCoverSourcePaths.size()) || index >= static_cast<int>(entryCoverStates.size())) {
    return false;
  }
  const std::string& path = entryPaths[index];
  if (isBadLibraryPath(path)) {
    entryCoverStates[index] = ENTRY_COVER_MISSING;
    return false;
  }
  if (entryCoverStates[index] == ENTRY_COVER_READY || entryCoverStates[index] == ENTRY_COVER_MISSING) {
    return false;
  }
  const int coverTargetWidth = libraryCoverTargetWidth();
  const int coverTargetHeight = libraryCoverTargetHeight();
  const std::string coverJobDetail =
      path + " " + std::to_string(coverTargetWidth) + "x" + std::to_string(coverTargetHeight);

  recordLibraryBreadcrumb(allowGeneration ? "cover_job_start" : "load cached thumbnail", basepath, coverJobDetail, index,
                          static_cast<int>(files.size()));
  std::string sourceCoverPath = entryCoverSourcePaths[index];
  if (sourceCoverPath.empty()) {
    if (FsHelpers::hasEpubExtension(path)) {
      sourceCoverPath = Epub(path, "/.crosspoint").getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(path)) {
      sourceCoverPath = Xtc(path, "/.crosspoint").getThumbBmpPath();
    }
  }

  bool updatedCachedRecord = false;
  if (auto* cached = findReusableThumbnailCacheRecord(path, sourceCoverPath, coverTargetWidth, coverTargetHeight,
                                                      &updatedCachedRecord)) {
    if (cached->state == ENTRY_COVER_READY && !cached->thumbPath.empty() && Storage.exists(cached->thumbPath.c_str())) {
      entryCoverStates[index] = ENTRY_COVER_READY;
      entryCoverPaths[index] = cached->thumbPath;
      entryCoverSourcePaths[index] = sourceCoverPath;
      if (updatedCachedRecord) {
        saveThumbnailCache();
      }
      recordLibraryBreadcrumb("cover_job_done", basepath, coverJobDetail, index, static_cast<int>(files.size()));
      return true;
    }
    if (cached->state == ENTRY_COVER_READY) {
      cached->state = ENTRY_COVER_UNKNOWN;
      cached->thumbPath.clear();
    } else {
      entryCoverStates[index] = cached->state;
      return false;
    }
  }

  const std::string existingThumbPath =
      UITheme::getCoverThumbPath(sourceCoverPath, coverTargetWidth, coverTargetHeight);
  if (!existingThumbPath.empty() && Storage.exists(existingThumbPath.c_str())) {
    entryCoverPaths[index] = existingThumbPath;
    entryCoverSourcePaths[index] = sourceCoverPath;
    entryCoverStates[index] = ENTRY_COVER_READY;
    rememberThumbnailCacheRecord(path, sourceCoverPath, existingThumbPath, coverTargetWidth, coverTargetHeight,
                                 ENTRY_COVER_READY);
    recordLibraryBreadcrumb("cover_job_done", basepath, coverJobDetail, index, static_cast<int>(files.size()));
    return true;
  }

  if (!allowGeneration) {
    return false;
  }
  if (ESP.getFreeHeap() < MIN_LIBRARY_COVER_HEAP || libraryFailureCount >= MAX_LIBRARY_FAILURES) {
    entryCoverStates[index] = ENTRY_COVER_MISSING;
    markBadLibraryPath(path);
    recordLibraryBreadcrumb("cover_job_failed", basepath, coverJobDetail, index, static_cast<int>(files.size()));
    return false;
  }
  if (isCoverWorkCancelled()) {
    return false;
  }

  const std::string generatedThumbPath =
      UITheme::ensureBookCoverThumbPath(path, sourceCoverPath, coverTargetWidth, coverTargetHeight,
                                        &LibraryActivity::coverWorkCancelledThunk, this);
  if (isCoverWorkCancelled()) {
    return false;
  }

  if (!generatedThumbPath.empty() && Storage.exists(generatedThumbPath.c_str())) {
    entryCoverPaths[index] = generatedThumbPath;
    entryCoverSourcePaths[index] = sourceCoverPath;
    entryCoverStates[index] = ENTRY_COVER_READY;
    rememberThumbnailCacheRecord(path, sourceCoverPath, generatedThumbPath, coverTargetWidth, coverTargetHeight,
                                 ENTRY_COVER_READY);
    recordLibraryBreadcrumb("cover_job_done", basepath, coverJobDetail, index, static_cast<int>(files.size()));
    return true;
  }

  entryCoverStates[index] = ENTRY_COVER_MISSING;
  markBadLibraryPath(path);
  rememberThumbnailCacheRecord(path, sourceCoverPath, "", coverTargetWidth, coverTargetHeight, ENTRY_COVER_MISSING);
  recordLibraryBreadcrumb("cover_job_failed", basepath, coverJobDetail, index, static_cast<int>(files.size()));
  return false;
}

bool LibraryActivity::resolveEntryCoverWithWatchdog(const int index, const bool allowGeneration) {
  constexpr unsigned long COVER_JOB_TIMEOUT_MS = 3500;
  const unsigned long startedAt = millis();
  const bool ok = resolveEntryCover(index, allowGeneration);
  const unsigned long elapsed = millis() - startedAt;
  if (elapsed > COVER_JOB_TIMEOUT_MS && index >= 0 && index < static_cast<int>(entryCoverStates.size())) {
    entryCoverStates[index] = ENTRY_COVER_MISSING;
    if (index < static_cast<int>(entryPaths.size())) {
      markBadLibraryPath(entryPaths[index]);
      rememberThumbnailCacheRecord(entryPaths[index],
                                   index < static_cast<int>(entryCoverSourcePaths.size())
                                       ? entryCoverSourcePaths[index]
                                       : std::string(),
                                   "", libraryCoverTargetWidth(), libraryCoverTargetHeight(), ENTRY_COVER_MISSING);
      recordLibraryBreadcrumb("cover_job_failed", basepath, entryPaths[index], index, static_cast<int>(files.size()));
    }
    return false;
  }
  return ok;
}

bool LibraryActivity::isCoverWorkCancelled() const {
  return mappedInput.isAnyMappedButtonPressed() || libraryWorkPaused || libraryRendering ||
         libraryFailureCount >= MAX_LIBRARY_FAILURES;
}

bool LibraryActivity::coverWorkCancelledThunk(void* context) {
  auto* activity = static_cast<LibraryActivity*>(context);
  return activity == nullptr || activity->isCoverWorkCancelled();
}

bool LibraryActivity::processVisibleCoverJob(const int pageItems) {
  const int activePageItems = std::min(pageItems, MAX_LIBRARY_ACTIVE_COVERS);
  if (libraryPostIndexCoverWarmup && libraryCoverWarmupPending && libraryFirstRenderDone) {
    beginPostIndexCoverWarmup(activePageItems);
  }
  if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && libraryCoverWarmupTotal == 0 &&
      libraryFirstRenderDone && usesBookshelfGrid() && activePageItems > 0) {
    updateActiveCoverWindow(activePageItems);
    buildCoverWarmupQueue(activePageItems);
    libraryCoverWarmupTotal = static_cast<uint16_t>(libraryCoverWarmupQueueCount);
    const int pending = countPendingCoverJobs(activePageItems);
    libraryCoverWarmupDone =
        static_cast<uint16_t>(std::min<int>(libraryCoverWarmupTotal, std::max(0, libraryCoverWarmupTotal - pending)));
    if (libraryCoverWarmupTotal > 0 && pending > 0) {
      recordLibraryBreadcrumb("cover warmup queue", basepath, "", static_cast<int>(libraryCoverWarmupDone),
                              static_cast<int>(libraryCoverWarmupTotal));
      libraryProgressHudOnlyRender = true;
      saveLibraryIndexCheckpoint("covers");
    } else if (libraryProgressAction == LIBRARY_PROGRESS_COVERS) {
      recordLibraryBreadcrumb("cover warmup done", basepath, "", 0, 0);
      libraryProgressAction = LIBRARY_PROGRESS_NONE;
      libraryPostIndexCoverWarmup = false;
      libraryCoverWarmupPending = false;
      libraryProgressHudOnlyRender = false;
      libraryCoverWarmupQueueCount = 0;
      saveLibraryIndexCheckpoint("complete");
      clearLibraryIndexCheckpoint();
      requestUpdate(true);
      return false;
    }
  }
  if (!usesBookshelfGrid() || activePageItems <= 0 || mappedInput.isAnyMappedButtonPressed() || libraryRendering ||
      libraryWorkPaused) {
    return false;
  }
  if (isLibraryDashboard() && libraryIndexingActive) {
    return false;
  }
  if (ESP.getFreeHeap() < MIN_LIBRARY_COVER_HEAP || libraryFailureCount >= MAX_LIBRARY_FAILURES) {
    return false;
  }
  const unsigned long sinceNavigation = millis() - lastNavigationInputMs;
  if (sinceNavigation < LIBRARY_CACHED_COVER_IDLE_MS) {
    return false;
  }
  const unsigned long now = millis();
  if (!libraryFirstRenderDone || now - lastLibraryRenderFinishedMs < LIBRARY_CACHED_COVER_IDLE_MS ||
      now - libraryEnteredAtMs < 220 || now - lastLibraryWorkMs < 95) {
    return false;
  }
  if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && libraryHotCoverWarmupStartMs == 0) {
    libraryHotCoverWarmupStartMs = now;
  }
  lastLibraryWorkMs = now;

  auto processCoverIndex = [this, activePageItems](const int index) -> bool {
    if (index >= 0 && index < static_cast<int>(entryTypes.size()) && entryTypes[index] == ENTRY_TYPE_AUTHOR_GROUP) {
      return false;
    }
    if (index < 0 || index >= static_cast<int>(entryCoverStates.size()) || !entryCanResolveCover(index)) {
      return false;
    }
    if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && index < static_cast<int>(entryPaths.size())) {
      libraryCurrentCoverTitle = getEntryTitle(index);
      recordLibraryBreadcrumb("cover warmup item", basepath,
                              libraryCurrentCoverTitle.empty() ? entryPaths[index] : libraryCurrentCoverTitle,
                              static_cast<int>(libraryCoverWarmupDone),
                              static_cast<int>(libraryCoverWarmupTotal));
    }
    auto markCoverWarmupProgress = [this, index](const char* phase) {
      if (libraryProgressAction != LIBRARY_PROGRESS_COVERS) {
        return;
      }
      if (libraryCoverWarmupTotal > 0 && libraryCoverWarmupDone < libraryCoverWarmupTotal) {
        ++libraryCoverWarmupDone;
      }
      recordLibraryBreadcrumb(phase, basepath,
                              index >= 0 && index < static_cast<int>(entryPaths.size()) ? entryPaths[index] : "",
                              static_cast<int>(libraryCoverWarmupDone),
                              static_cast<int>(libraryCoverWarmupTotal));
      libraryProgressHudOnlyRender = true;
    };
    if (entryCoverStates[index] == ENTRY_COVER_UNKNOWN) {
      if (millis() - lastNavigationInputMs < LIBRARY_BACKGROUND_IDLE_MS) {
        return false;
      }
      resolveEntryCoverWithWatchdog(index, true);
      if (mappedInput.isAnyMappedButtonPressed() || libraryWorkPaused) {
        return false;
      }
      updateActiveCoverWindow(activePageItems);
      if (index < static_cast<int>(entryPaths.size()) && index < static_cast<int>(entryCoverPaths.size()) &&
          entryCoverStates[index] == ENTRY_COVER_READY && !entryCoverPaths[index].empty()) {
        if (index < static_cast<int>(resolvedRows.size())) {
          resolvedRows[index].coverPath = entryCoverPaths[index];
        }
        if (!libraryCoverManager.loadRendered(entryPaths[index], entryCoverPaths[index])) {
          entryCoverStates[index] = ENTRY_COVER_MISSING;
          if (index < static_cast<int>(resolvedRows.size())) {
            resolvedRows[index].coverPath.clear();
          }
        }
      }
      if (entryCoverStates[index] == ENTRY_COVER_READY) {
        markCoverWarmupProgress("cover warmup ready");
      } else if (entryCoverStates[index] == ENTRY_COVER_MISSING) {
        markCoverWarmupProgress("cover warmup skip");
      }
      libraryDirtyCoverIndex = index;
      requestUpdate();
      return true;
    }
    if (entryCoverStates[index] == ENTRY_COVER_READY && index < static_cast<int>(entryPaths.size()) &&
        index < static_cast<int>(entryCoverPaths.size()) && !entryCoverPaths[index].empty() &&
        !libraryCoverManager.hasRendered(entryPaths[index], entryCoverPaths[index])) {
      updateActiveCoverWindow(activePageItems);
      if (mappedInput.isAnyMappedButtonPressed() || libraryWorkPaused) {
        return false;
      }
      if (libraryCoverManager.loadRendered(entryPaths[index], entryCoverPaths[index])) {
        if (index < static_cast<int>(resolvedRows.size())) {
          resolvedRows[index].coverPath = entryCoverPaths[index];
        }
        markCoverWarmupProgress("cover warmup ready");
        libraryDirtyCoverIndex = index;
        requestUpdate();
        return true;
      }
      entryCoverStates[index] = ENTRY_COVER_MISSING;
      if (index < static_cast<int>(resolvedRows.size())) {
        resolvedRows[index].coverPath.clear();
      }
      markCoverWarmupProgress("cover warmup skip");
      libraryDirtyCoverIndex = index;
      requestUpdate();
      return true;
    }
    return false;
  };

  if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && libraryCoverWarmupQueueCount > 0) {
    for (uint8_t queueIndex = 0; queueIndex < libraryCoverWarmupQueueCount; ++queueIndex) {
      const auto& item = libraryCoverWarmupQueue[queueIndex];
      if (item.index < 0 || item.path[0] == '\0') {
        continue;
      }
      if (item.index >= static_cast<int>(entryPaths.size()) || entryPaths[item.index] != item.path) {
        recordLibraryBreadcrumb("cover warmup skip", basepath, item.path, item.index,
                                static_cast<int>(libraryCoverWarmupTotal));
        if (libraryCoverWarmupDone < libraryCoverWarmupTotal) {
          ++libraryCoverWarmupDone;
        }
        continue;
      }
      if (processCoverIndex(item.index)) {
        return true;
      }
    }
    if (countPendingCoverJobs(activePageItems) <= 0) {
      libraryCoverWarmupDone = libraryCoverWarmupTotal;
      recordLibraryBreadcrumb("cover warmup done", basepath, "", static_cast<int>(libraryCoverWarmupDone),
                              static_cast<int>(libraryCoverWarmupTotal));
      libraryProgressAction = LIBRARY_PROGRESS_NONE;
      libraryPostIndexCoverWarmup = false;
      libraryCoverWarmupPending = false;
      libraryProgressHudOnlyRender = false;
      libraryCoverWarmupQueueCount = 0;
      saveLibraryIndexCheckpoint("complete");
      clearLibraryIndexCheckpoint();
      requestUpdate(true);
    }
    libraryCoverManager.cancelPrefetch();
    return false;
  }

  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) {
      return false;
    }
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    const int pageStartPosition = (currentPosition / activePageItems) * activePageItems;
    const int pageEndPosition = std::min(static_cast<int>(visibleIndices.size()), pageStartPosition + activePageItems);
    for (int position = pageStartPosition; position < pageEndPosition; ++position) {
      const int index = visibleIndices[position];
      if (index < 0) continue;
      if (processCoverIndex(index)) {
        return true;
      }
    }
    if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && countPendingCoverJobs(activePageItems) <= 0) {
      libraryCoverWarmupDone = libraryCoverWarmupTotal;
      recordLibraryBreadcrumb("cover warmup done", basepath, "", static_cast<int>(libraryCoverWarmupDone),
                              static_cast<int>(libraryCoverWarmupTotal));
      libraryProgressAction = LIBRARY_PROGRESS_NONE;
      libraryPostIndexCoverWarmup = false;
      libraryCoverWarmupPending = false;
      libraryProgressHudOnlyRender = false;
      libraryCoverWarmupQueueCount = 0;
      saveLibraryIndexCheckpoint("complete");
      clearLibraryIndexCheckpoint();
      requestUpdate(true);
    }
    libraryCoverManager.cancelPrefetch();
    return false;
  }

  const int pageStartIndex = (static_cast<int>(selectorIndex) / activePageItems) * activePageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + activePageItems);
  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    if (processCoverIndex(index)) {
      return true;
    }
  }
  if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && countPendingCoverJobs(activePageItems) <= 0) {
    libraryCoverWarmupDone = libraryCoverWarmupTotal;
    recordLibraryBreadcrumb("cover warmup done", basepath, "", static_cast<int>(libraryCoverWarmupDone),
                            static_cast<int>(libraryCoverWarmupTotal));
    libraryProgressAction = LIBRARY_PROGRESS_NONE;
    libraryPostIndexCoverWarmup = false;
    libraryCoverWarmupPending = false;
    libraryProgressHudOnlyRender = false;
    libraryCoverWarmupQueueCount = 0;
    saveLibraryIndexCheckpoint("complete");
    clearLibraryIndexCheckpoint();
    requestUpdate(true);
  }
  libraryCoverManager.cancelPrefetch();
  return false;
}

int LibraryActivity::countPendingCoverJobs(const int pageItems) const {
  const int activePageItems = std::min(pageItems, MAX_LIBRARY_ACTIVE_COVERS);
  if (!usesBookshelfGrid() || activePageItems <= 0) {
    return 0;
  }
  if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && libraryCoverWarmupQueueCount > 0) {
    int pending = 0;
    for (uint8_t queueIndex = 0; queueIndex < libraryCoverWarmupQueueCount; ++queueIndex) {
      const auto& item = libraryCoverWarmupQueue[queueIndex];
      const int index = item.index;
      if (index < 0 || index >= static_cast<int>(entryCoverStates.size()) || index >= static_cast<int>(entryPaths.size()) ||
          index >= static_cast<int>(entryCoverPaths.size())) {
        continue;
      }
      if (entryCoverStates[index] == ENTRY_COVER_UNKNOWN) {
        ++pending;
      } else if (entryCoverStates[index] == ENTRY_COVER_READY && !entryCoverPaths[index].empty() &&
                 !libraryCoverManager.hasRendered(entryPaths[index], entryCoverPaths[index])) {
        ++pending;
      }
    }
    return pending;
  }
  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) {
      return 0;
    }
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    const int pageStartPosition = (currentPosition / activePageItems) * activePageItems;
    const int pageEndPosition = std::min(static_cast<int>(visibleIndices.size()), pageStartPosition + activePageItems);
    int pending = 0;
    for (int position = pageStartPosition; position < pageEndPosition; ++position) {
      const int index = visibleIndices[position];
      if (index >= 0 && index < static_cast<int>(entryCoverStates.size()) && entryCanResolveCover(index)) {
        if (entryCoverStates[index] == ENTRY_COVER_UNKNOWN) {
          ++pending;
        } else if (entryCoverStates[index] == ENTRY_COVER_READY && index < static_cast<int>(entryPaths.size()) &&
                   index < static_cast<int>(entryCoverPaths.size()) && !entryCoverPaths[index].empty() &&
                   !libraryCoverManager.hasRendered(entryPaths[index], entryCoverPaths[index])) {
          ++pending;
        }
      }
    }
    return pending;
  }
  const int pageStartIndex = (static_cast<int>(selectorIndex) / activePageItems) * activePageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + activePageItems);
  int pending = 0;
  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    if (index < static_cast<int>(entryCoverStates.size()) && entryCanResolveCover(index)) {
      if (entryCoverStates[index] == ENTRY_COVER_UNKNOWN) {
        ++pending;
      } else if (entryCoverStates[index] == ENTRY_COVER_READY && index < static_cast<int>(entryPaths.size()) &&
                 index < static_cast<int>(entryCoverPaths.size()) && !entryCoverPaths[index].empty() &&
                 !libraryCoverManager.hasRendered(entryPaths[index], entryCoverPaths[index])) {
        ++pending;
      }
    }
  }
  return pending;
}

int LibraryActivity::countCoverWindowCandidates(const int pageItems) const {
  const int activePageItems = std::min(pageItems, MAX_LIBRARY_ACTIVE_COVERS);
  if (!usesBookshelfGrid() || activePageItems <= 0) {
    return 0;
  }
  int candidates = 0;
  auto countIndex = [this, &candidates](const int index) {
    if (candidates >= MAX_LIBRARY_ACTIVE_COVERS) {
      return;
    }
    if (entryCanResolveCover(index)) {
      ++candidates;
    }
  };

  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) {
      return 0;
    }
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    const int pageStartPosition = (currentPosition / activePageItems) * activePageItems;
    const int pageEndPosition = std::min(static_cast<int>(visibleIndices.size()), pageStartPosition + activePageItems);
    for (int position = pageStartPosition; position < pageEndPosition; ++position) {
      countIndex(visibleIndices[position]);
    }
    return candidates;
  }

  const int pageStartIndex = (static_cast<int>(selectorIndex) / activePageItems) * activePageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + activePageItems);
  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    countIndex(index);
  }
  return candidates;
}

void LibraryActivity::buildCoverWarmupQueue(const int pageItems) {
  libraryCoverWarmupQueueCount = 0;
  const int activePageItems = std::min(pageItems, MAX_LIBRARY_ACTIVE_COVERS);
  if (!usesBookshelfGrid() || activePageItems <= 0) {
    recordLibraryBreadcrumb("cover warmup empty", basepath, "<empty>", 0, 0);
    return;
  }
  recordLibraryBreadcrumb("cover warmup prepare", basepath, "<empty>", static_cast<int>(selectorIndex),
                          static_cast<int>(files.size()));

  auto enqueueIndex = [this](const int index, const int cardIndex, const int visibleCount) {
    if (libraryCoverWarmupQueueCount >= LIBRARY_COVER_WARMUP_CAPACITY) {
      return;
    }
    const std::string path = index >= 0 && index < static_cast<int>(entryPaths.size()) ? entryPaths[index] : "";
    recordLibraryBreadcrumb("cover warmup validate row", basepath, path.empty() ? "<empty>" : path, index, cardIndex);
    const bool validIndex = index >= 0 && index < static_cast<int>(files.size()) &&
                            index < static_cast<int>(entryPaths.size()) &&
                            index < static_cast<int>(entryTypes.size()) &&
                            index < static_cast<int>(entryCoverStates.size());
    const bool validCard = cardIndex >= 0 && cardIndex < MAX_LIBRARY_ACTIVE_COVERS &&
                           (!libraryHasRetainedGridRects ||
                            (index >= 0 && index < static_cast<int>(libraryRetainedItemRects.size()) &&
                             libraryRetainedItemRects[index].width > 0 && libraryRetainedItemRects[index].height > 0));
    const bool validPathLength = warmupFieldFits(sizeof(libraryCoverWarmupQueue[0].path), path);
    const bool validBook = validIndex && validPathLength && entryTypes[index] == ENTRY_TYPE_BOOK && !files[index].empty() &&
                           files[index].back() != '/' && !path.empty() && FsHelpers::hasEpubExtension(path) &&
                           !pathHasIgnoredLibraryFolder(path);
    if (!validCard || !validBook) {
      recordLibraryBreadcrumb("cover warmup skip", basepath, path.empty() ? "<empty>" : path, index,
                              static_cast<int>(libraryCoverWarmupQueueCount));
      return;
    }
    auto& item = libraryCoverWarmupQueue[libraryCoverWarmupQueueCount];
    item = LibraryCoverWarmupItem{};
    item.index = index;
    item.cardIndex = cardIndex;
    copyWarmupField(item.path, sizeof(item.path), path);
    copyWarmupField(item.title, sizeof(item.title), getEntryTitle(index));
    copyWarmupField(item.author, sizeof(item.author), getEntrySubtitle(index));
    if (index < static_cast<int>(entryCoverPaths.size())) {
      if (warmupFieldFits(sizeof(item.coverPath), entryCoverPaths[index])) {
        copyWarmupField(item.coverPath, sizeof(item.coverPath), entryCoverPaths[index]);
      }
    }
    ++libraryCoverWarmupQueueCount;
    recordLibraryBreadcrumb("cover warmup push", basepath, item.path, cardIndex,
                            static_cast<int>(libraryCoverWarmupQueueCount));
    (void)visibleCount;
  };

  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    recordLibraryBreadcrumb("cover warmup visible rows", basepath, "<empty>", static_cast<int>(visibleIndices.size()),
                            static_cast<int>(libraryCoverWarmupQueueCount));
    if (visibleIndices.empty()) {
      recordLibraryBreadcrumb("cover warmup empty", basepath, "<empty>", 0, 0);
      return;
    }
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    const int pageStartPosition = (currentPosition / activePageItems) * activePageItems;
    const int pageEndPosition = std::min(static_cast<int>(visibleIndices.size()), pageStartPosition + activePageItems);
    for (int position = pageStartPosition; position < pageEndPosition; ++position) {
      enqueueIndex(visibleIndices[position], position - pageStartPosition, pageEndPosition - pageStartPosition);
    }
    if (libraryCoverWarmupQueueCount == 0) {
      recordLibraryBreadcrumb("cover warmup empty", basepath, "<empty>", 0, 0);
    } else {
      recordLibraryBreadcrumb("cover warmup ready", basepath, libraryCoverWarmupQueue[0].path,
                              static_cast<int>(visibleIndices.size()),
                              static_cast<int>(libraryCoverWarmupQueueCount));
    }
    return;
  }

  const int pageStartIndex = (static_cast<int>(selectorIndex) / activePageItems) * activePageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + activePageItems);
  recordLibraryBreadcrumb("cover warmup visible rows", basepath, "<empty>", pageEndIndex - pageStartIndex,
                          static_cast<int>(libraryCoverWarmupQueueCount));
  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    enqueueIndex(index, index - pageStartIndex, pageEndIndex - pageStartIndex);
  }
  if (libraryCoverWarmupQueueCount == 0) {
    recordLibraryBreadcrumb("cover warmup empty", basepath, "<empty>", 0, 0);
  } else {
    recordLibraryBreadcrumb("cover warmup ready", basepath, libraryCoverWarmupQueue[0].path,
                            pageEndIndex - pageStartIndex, static_cast<int>(libraryCoverWarmupQueueCount));
  }
}

void LibraryActivity::beginPostIndexCoverWarmup(const int pageItems) {
  const int activePageItems = std::min(pageItems, MAX_LIBRARY_ACTIVE_COVERS);
  if (!libraryPostIndexCoverWarmup || !libraryCoverWarmupPending || !libraryFirstRenderDone ||
      !usesBookshelfGrid() || activePageItems <= 0 || files.empty()) {
    return;
  }

  recordLibraryBreadcrumb("cover warmup prepare", basepath, "", static_cast<int>(selectorIndex),
                          static_cast<int>(files.size()));
  updateActiveCoverWindow(activePageItems);
  buildCoverWarmupQueue(activePageItems);
  libraryCoverWarmupTotal = static_cast<uint16_t>(libraryCoverWarmupQueueCount);
  const int pending = countPendingCoverJobs(activePageItems);
  libraryCoverWarmupDone =
      static_cast<uint16_t>(std::min<int>(libraryCoverWarmupTotal, std::max(0, libraryCoverWarmupTotal - pending)));
  libraryCoverWarmupPending = false;
  libraryCurrentCoverTitle.clear();
  if (libraryCoverWarmupTotal == 0 || pending <= 0) {
    recordLibraryBreadcrumb("cover warmup done", basepath, "", static_cast<int>(libraryCoverWarmupDone),
                            static_cast<int>(libraryCoverWarmupTotal));
    libraryPostIndexCoverWarmup = false;
    libraryProgressAction = LIBRARY_PROGRESS_NONE;
    libraryProgressHudOnlyRender = false;
    return;
  }

  recordLibraryBreadcrumb("cover warmup queue", basepath, "", static_cast<int>(libraryCoverWarmupDone),
                          static_cast<int>(libraryCoverWarmupTotal));
  libraryProgressAction = LIBRARY_PROGRESS_COVERS;
  libraryProgressHudOnlyRender = true;
  libraryHotCoverWarmupStartMs = millis();
  saveLibraryIndexCheckpoint("covers");
  requestUpdate();
}

std::vector<LibraryCoverWindowEntry> LibraryActivity::collectCoverWindowEntries(const int pageItems,
                                                                                const bool firstPage) const {
  std::vector<LibraryCoverWindowEntry> entries;
  const int activePageItems = std::min(pageItems, MAX_LIBRARY_ACTIVE_COVERS);
  if (!usesBookshelfGrid() || activePageItems <= 0) {
    return entries;
  }
  entries.reserve(static_cast<size_t>(activePageItems));
  auto addEntry = [this, &entries](const int index) {
    if (entries.size() >= static_cast<size_t>(MAX_LIBRARY_ACTIVE_COVERS)) {
      return;
    }
    if (index < 0 || index >= static_cast<int>(entryPaths.size()) || entryPaths[index].empty()) {
      return;
    }
    LibraryCoverWindowEntry entry;
    entry.bookPath = entryPaths[index];
    if (index < static_cast<int>(entryCoverPaths.size())) {
      entry.thumbPath = entryCoverPaths[index];
    }
    if (index < static_cast<int>(entryCoverStates.size())) {
      entry.state = entryCoverStates[index];
    }
    entries.push_back(std::move(entry));
  };

  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) {
      return entries;
    }
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    const int currentPageStart = firstPage ? 0 : (currentPosition / activePageItems) * activePageItems;
    const int start = currentPageStart;
    if (start >= static_cast<int>(visibleIndices.size())) {
      return entries;
    }
    const int end = std::min(static_cast<int>(visibleIndices.size()), start + activePageItems);
    for (int position = start; position < end; ++position) {
      addEntry(visibleIndices[position]);
    }
    return entries;
  }

  const int currentPageStart = firstPage ? 0 : (static_cast<int>(selectorIndex) / activePageItems) * activePageItems;
  const int start = currentPageStart;
  if (start >= static_cast<int>(files.size())) {
    return entries;
  }
  const int end = std::min(static_cast<int>(files.size()), start + activePageItems);
  for (int index = start; index < end; ++index) {
    addEntry(index);
  }
  return entries;
}

void LibraryActivity::updateActiveCoverWindow(const int pageItems) {
  const int activePageItems = std::min(pageItems, MAX_LIBRARY_ACTIVE_COVERS);
  if (!usesBookshelfGrid() || activePageItems <= 0) {
    libraryCoverManager.clearWindows();
    return;
  }
  libraryCoverManager.setActiveWindow(collectCoverWindowEntries(activePageItems, false));
  libraryCoverManager.setFirstPageWindow(collectCoverWindowEntries(activePageItems, true));
}

void LibraryActivity::onEnter() {
  Activity::onEnter();
  libraryHotEnterStartMs = millis();
  libraryHotRowsReadyMs = 0;
  libraryHotFirstGridDrawMs = 0;
  libraryHotCoverWarmupStartMs = 0;
  lastNavigationInputMs = millis();
  libraryEnteredAtMs = millis();
  lastLibraryWorkMs = millis();
  lastLibraryRenderFinishedMs = millis();
  libraryFirstRenderDone = false;
  libraryRendering = false;
  libraryWorkPaused = false;
  libraryScanRequested = false;
  librarySkippedFolderName.clear();
  libraryBreadcrumbClearedThisSession = false;
  forceLibraryLayout3x3();

  if (isBookshelfMode() && rawFilesLaunch) {
    libraryView = LIBRARY_VIEW_FILES;
  }
  const bool restoredPersistentLibraryState = !rawFilesLaunch && !hasSavedBrowserCursor && basepath == "/";
  if (!rawFilesLaunch && !hasSavedBrowserCursor && basepath == "/") {
    loadLibraryRenderState();
  }
  if (!rawFilesLaunch && hasSavedBrowserCursor && basepath == "/") {
    basepath = savedBrowserBasepath;
    libraryView = savedBrowserLibraryView;
    selectorIndex = savedBrowserSelectorIndex;
  } else if (!restoredPersistentLibraryState) {
    selectorIndex = 0;
  }
  if (launchMode == LaunchMode::Reindex) {
    rawFilesLaunch = false;
    basepath = "/";
    libraryView = LIBRARY_VIEW_DASHBOARD;
    libraryFilterMode = LIBRARY_FILTER_ALL;
    libraryAuthorViewKey.clear();
    libraryAuthorViewName.clear();
    librarySeriesViewName.clear();
    selectorIndex = 0;
    libraryProgressAction = LIBRARY_PROGRESS_REBUILD;
    resetLibraryDashboardState(true);
    libraryScanRequested = true;
  } else if (launchMode == LaunchMode::Authors) {
    rawFilesLaunch = false;
    basepath = "/";
    libraryView = LIBRARY_VIEW_DASHBOARD;
    libraryFilterMode = LIBRARY_FILTER_ALL;
    SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_AUTHOR;
    libraryAuthorViewKey.clear();
    libraryAuthorViewName.clear();
    librarySeriesViewName.clear();
    selectorIndex = 0;
  } else if (launchMode == LaunchMode::ToRead) {
    rawFilesLaunch = false;
    basepath = "/";
    libraryView = LIBRARY_VIEW_TO_READ;
    libraryFilterMode = LIBRARY_FILTER_TO_READ;
    libraryAuthorViewKey.clear();
    libraryAuthorViewName.clear();
    librarySeriesViewName.clear();
    selectorIndex = 0;
  } else if (launchMode == LaunchMode::Finished) {
    rawFilesLaunch = false;
    basepath = "/";
    libraryView = LIBRARY_VIEW_FINISHED;
    libraryFilterMode = LIBRARY_FILTER_FINISHED;
    libraryAuthorViewKey.clear();
    libraryAuthorViewName.clear();
    librarySeriesViewName.clear();
    selectorIndex = 0;
  } else if (launchMode == LaunchMode::Series) {
    rawFilesLaunch = false;
    basepath = "/";
    libraryView = LIBRARY_VIEW_DASHBOARD;
    libraryFilterMode = LIBRARY_FILTER_ALL;
    libraryAuthorViewKey.clear();
    libraryAuthorViewName.clear();
    librarySeriesViewName.clear();
    selectorIndex = 0;
    pendingSeriesCollectionPicker = true;
  }
  confirmLongPressHandled = false;
  backLongPressHandled = false;
  holdPreviewVisible = false;

  if (!rawFilesLaunch && isBookshelfMode() && basepath == "/") {
    if (libraryView == 0) {
      libraryView = LIBRARY_VIEW_DASHBOARD;
    }
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const int hudW = std::min(300, pageWidth - 48);
    const int hudH = 74;
    const int hudX = (pageWidth - hudW) / 2;
    const int hudY = (pageHeight - hudH) / 2;
    renderer.fillRoundedRect(hudX, hudY, hudW, hudH, 8, Color::White);
    renderer.drawRoundedRect(hudX, hudY, hudW, hudH, 2, 8, true);
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, tr(STR_OPENING_LIBRARY), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, hudX + std::max(0, (hudW - textW) / 2), hudY + 25, tr(STR_OPENING_LIBRARY), true,
                      EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    loadFiles();
    requestUpdate();
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    libraryView = 0;
    loadFiles();
  } else if (!root.isDirectory()) {
    root.close();
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    if (basepath != "/" && !isRawBrowseFilesMode()) libraryView = 0;
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    root.close();
    if (basepath != "/" && !isRawBrowseFilesMode()) libraryView = 0;
    loadFiles();
  }

  requestUpdate();
}

void LibraryActivity::onExit() {
  Activity::onExit();
  libraryWorkPaused = true;
  libraryRendering = false;
  clearLibrarySearchSession(true);
  if (isLibraryDashboard() && libraryFilterMode == LIBRARY_FILTER_ALL) {
    saveLibraryDashboardSnapshot();
    if (!libraryIndexingActive) {
      saveLibraryDashboardIndex();
    }
    clearLibraryBreadcrumb();
  } else if (librarySafeMode) {
    recordLibraryBreadcrumb("safe-mode", basepath, "", static_cast<int>(selectorIndex),
                            static_cast<int>(files.size()));
  }
  libraryIndexingActive = false;
  libraryIndexStage = LIBRARY_INDEX_STAGE_IDLE;
  libraryScanFolders.clear();
  libraryScanOffsets.clear();
  libraryDiscoveredBookPaths.clear();
  libraryMetadataResolveIndex = 0;
  libraryCurrentScanFolder.clear();
  librarySkippedFolderName.clear();
  if (!rawFilesLaunch) {
    saveLibraryRenderState();
    savedBrowserBasepath = basepath;
    savedBrowserLibraryView = libraryView;
    savedBrowserSelectorIndex = selectorIndex;
    hasSavedBrowserCursor = true;
  }
  files.clear();
  completedFileStates.clear();
  progressFileStates.clear();
  entryRecentReadAt.clear();
  libraryFileStates.clear();
  folderItemCounts.clear();
  entryPaths.clear();
  entryTitles.clear();
  entrySubtitles.clear();
  entryCoverPaths.clear();
  entryCoverSourcePaths.clear();
  entryCoverStates.clear();
  entryTypes.clear();
  authorGroupCoverPaths.clear();
  resolvedRows.clear();
  if (rawFilesLaunch || !isBookshelfMode()) {
    libraryCoverManager.clearWindows();
  }
}

bool LibraryActivity::preventAutoSleep() {
  return libraryIndexingActive || libraryProgressAction == LIBRARY_PROGRESS_REFRESH ||
         libraryProgressAction == LIBRARY_PROGRESS_REBUILD ||
         libraryProgressAction == LIBRARY_PROGRESS_COVERS;
}

void LibraryActivity::clearFileMetadata(const std::string& fullPath) {
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(fullPath)) {
    Xtc(fullPath, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasTxtExtension(fullPath) || FsHelpers::hasMarkdownExtension(fullPath)) {
    Txt(fullPath, "/.crosspoint").clearCache();
  } else {
    return;
  }
  LOG_DBG("FileBrowser", "Cleared metadata cache for: %s", fullPath.c_str());
}

void LibraryActivity::loop() {
  if (pendingSeriesCollectionPicker) {
    pendingSeriesCollectionPicker = false;
    openSeriesCollectionPicker();
    return;
  }

  if (libraryOverlayMode != LIBRARY_OVERLAY_NONE) {
    libraryWorkPaused = true;
    if (handleLibraryOverlayInput()) return;
  }

  if (libraryIndexSummaryVisible &&
      (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
       mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
    libraryIndexSummaryVisible = false;
    requestUpdate(true);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    backLongPressHandled = false;
    sortPreviewVisible = false;
    libraryWorkPaused = true;
    if (libraryProgressAction != LIBRARY_PROGRESS_NONE || libraryIndexingActive) {
      if (libraryProgressAction == LIBRARY_PROGRESS_COVERS && !libraryIndexingActive) {
        libraryCoverManager.cancelPrefetch();
        libraryProgressAction = LIBRARY_PROGRESS_NONE;
        libraryPostIndexCoverWarmup = false;
        libraryCoverWarmupPending = false;
        libraryCoverWarmupQueueCount = 0;
        libraryCurrentCoverTitle.clear();
        libraryProgressHudOnlyRender = false;
        clearLibraryIndexCheckpoint();
        requestUpdate(true);
        return;
      }
      libraryIndexingActive = false;
      libraryScanRequested = false;
      libraryScanFolders.clear();
      libraryScanOffsets.clear();
      libraryDiscoveredBookPaths.clear();
      libraryMetadataResolveIndex = 0;
      libraryIndexStage = LIBRARY_INDEX_STAGE_IDLE;
      libraryCurrentScanFolder.clear();
      libraryCoverManager.cancelPrefetch();
      libraryProgressAction = LIBRARY_PROGRESS_NONE;
      libraryPostIndexCoverWarmup = false;
      libraryCoverWarmupPending = false;
      libraryCoverWarmupQueueCount = 0;
      libraryIndexCanceled = true;
      libraryIndexSummaryVisible = true;
      libraryProgressBookCount = static_cast<uint16_t>(std::min<size_t>(65535, files.size()));
      clearLibraryIndexCheckpoint();
      requestUpdate(true);
      return;
    }
  }

  if (librarySafeMode && basepath == "/" && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      !backLongPressHandled && mappedInput.getHeldTime() >= GO_HOME_MS) {
    backLongPressHandled = true;
    mappedInput.consumeActiveHoldUntilRelease();
    librarySafeMode = false;
    rawFilesLaunch = false;
    libraryView = LIBRARY_VIEW_DASHBOARD;
    clearLibraryBreadcrumb();
    loadFiles();
    selectorIndex = 0;
    requestUpdate(true);
    return;
  }

  const bool backCanOpenSortView =
      !librarySafeMode && isBookshelfMode() && basepath == "/" && (isLibraryDashboard() || isRawBrowseFilesMode());

  if (backCanOpenSortView && mappedInput.isPressed(MappedInputManager::Button::Back) && !backLongPressHandled &&
      mappedInput.getHeldTime() >= HOLD_PREVIEW_MS && mappedInput.getHeldTime() < GO_HOME_MS && !sortPreviewVisible &&
      !lockLongPressBack) {
    sortPreviewVisible = true;
    requestUpdate();
  }

  if (backCanOpenSortView && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      !backLongPressHandled && mappedInput.getHeldTime() >= GO_HOME_MS && !lockLongPressBack) {
    backLongPressHandled = true;
    sortPreviewVisible = false;
    mappedInput.consumeActiveHoldUntilRelease();
    openSortViewMenu();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backLongPressHandled) {
    backLongPressHandled = false;
    sortPreviewVisible = false;
    libraryWorkPaused = false;
    return;
  }

  // Long press BACK (1s+) goes to root folder
  // but Long press BACK (1s+) from ReaderActivity sends us here with the MappedInput already set.
  // So ignore it the first time.
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/" && !lockLongPressBack) {
    lastNavigationInputMs = millis();
    basepath = "/";
    libraryView = isRawBrowseFilesMode() ? LIBRARY_VIEW_FILES : (isBookshelfMode() ? LIBRARY_VIEW_DASHBOARD : 0);
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    sortPreviewVisible = false;
    libraryWorkPaused = false;
    return;
  }

  if (!librarySearchQuery.empty() && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    clearLibrarySearchSession(true);
    requestUpdate(true);
    return;
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const int pageItems = getPageItems(contentHeight);
  if (libraryProgressAction == LIBRARY_PROGRESS_COVERS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      libraryCoverManager.cancelPrefetch();
      libraryProgressAction = LIBRARY_PROGRESS_NONE;
      libraryPostIndexCoverWarmup = false;
      libraryCoverWarmupPending = false;
      libraryCoverWarmupTotal = 0;
      libraryCoverWarmupDone = 0;
      libraryCurrentCoverTitle.clear();
      libraryCoverWarmupQueueCount = 0;
      libraryProgressHudOnlyRender = false;
      requestUpdate(true);
      return;
    }
    libraryWorkPaused = false;
    processVisibleCoverJob(pageItems);
    return;
  }

  const bool hasSelection = !files.empty() && selectorIndex < files.size();
  const std::string selectedEntry = hasSelection ? files[selectorIndex] : "";
  const bool selectedIsDirectory = !selectedEntry.empty() && selectedEntry.back() == '/';
  const bool selectedSupportedBook =
      hasSelection && selectorIndex < entryPaths.size() &&
      selectorIndex < entryTypes.size() && entryTypes[selectorIndex] == ENTRY_TYPE_BOOK &&
      (FsHelpers::hasEpubExtension(entryPaths[selectorIndex]) || FsHelpers::hasXtcExtension(entryPaths[selectorIndex]) ||
       FsHelpers::hasTxtExtension(entryPaths[selectorIndex]) || FsHelpers::hasMarkdownExtension(entryPaths[selectorIndex]));
  const bool selectedSupportsBookActions =
      hasSelection && (isBookshelfMode() || rawFilesLaunch) && !selectedIsDirectory && selectedSupportedBook &&
      (!isLibraryDashboard() ||
       (selectorIndex < libraryFileStates.size() && libraryFileStates[selectorIndex] < LIBRARY_STATE_SECTION));

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmLongPressHandled = false;
    holdPreviewVisible = false;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && selectedSupportsBookActions &&
      !confirmLongPressHandled && mappedInput.getHeldTime() >= HOLD_PREVIEW_MS &&
      mappedInput.getHeldTime() < BOOK_ACTION_HOLD_MS && !holdPreviewVisible) {
    holdPreviewVisible = true;
    requestUpdate();
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && selectedSupportsBookActions &&
      !confirmLongPressHandled && mappedInput.getHeldTime() >= BOOK_ACTION_HOLD_MS) {
    confirmLongPressHandled = true;
    holdPreviewVisible = false;
    mappedInput.consumeActiveHoldUntilRelease();
    openBookActions(selectorIndex);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lastNavigationInputMs = millis();
    if (holdPreviewVisible) {
      holdPreviewVisible = false;
      requestUpdate();
    }
    if (confirmLongPressHandled) {
      confirmLongPressHandled = false;
      return;
    }
    if (files.empty()) return;
    clampSelector();

    const std::string& entry = files[selectorIndex];
    bool isDirectory = !entry.empty() && entry.back() == '/';

    if (isBookshelfMode() && isLibraryDashboard()) {
      if (selectorIndex < entryTypes.size() && entryTypes[selectorIndex] == ENTRY_TYPE_AUTHOR_GROUP) {
        libraryAuthorViewKey = selectorIndex < entryPaths.size() ? entryPaths[selectorIndex] : "";
        libraryAuthorViewName = getEntryTitle(static_cast<int>(selectorIndex));
        libraryAuthorGroupSelectorIndex = selectorIndex;
        libraryView = LIBRARY_VIEW_AUTHOR;
        selectorIndex = 0;
        saveLibraryRenderState();
        loadFiles();
        requestUpdate(true);
        return;
      }
      const uint8_t targetView =
          selectorIndex < libraryFileStates.size() ? libraryFileStates[selectorIndex] : LIBRARY_VIEW_FILES;
      const bool selectedSection =
          selectorIndex < entryPaths.size() && entryPaths[selectorIndex].empty() &&
          (targetView == LIBRARY_VIEW_TO_READ || targetView == LIBRARY_VIEW_FINISHED || targetView == LIBRARY_VIEW_FILES);
      if (selectedSection && targetView == LIBRARY_VIEW_FILES) {
        libraryView = LIBRARY_VIEW_FILES;
        rawFilesLaunch = true;
      } else if (selectedSection && (targetView == LIBRARY_VIEW_TO_READ || targetView == LIBRARY_VIEW_FINISHED)) {
        libraryView = targetView;
      } else {
        if (selectorIndex >= entryPaths.size() || entryPaths[selectorIndex].empty()) return;
        if (isDirectory) {
          basepath = entryPaths[selectorIndex];
          if (basepath.size() > 1 && basepath.back() == '/') basepath.pop_back();
          libraryView = LIBRARY_VIEW_FILES;
          selectorIndex = 0;
          loadFiles();
          requestUpdate();
          return;
        }
        clearLibrarySearchSession(false);
        onSelectBook(entryPaths[selectorIndex]);
        return;
      }
      selectorIndex = 0;
      loadFiles();
      requestUpdate();
      return;
    }

    if (isBookshelfMode() && (isLibraryShelf() || isLibraryAuthorView())) {
      if (selectorIndex >= entryPaths.size() || entryPaths[selectorIndex].empty()) return;
      clearLibrarySearchSession(false);
      onSelectBook(entryPaths[selectorIndex]);
      return;
    }

    if (mappedInput.getHeldTime() >= GO_HOME_MS && !isDirectory) {
      // --- LONG PRESS ACTION: DELETE FILE ---
      if (!isBookshelfMode()) {
        const std::string fullPath = getFullPathForEntry(entry);
        confirmDeleteFile(fullPath, entry);
      }
      return;
    }

    if (basepath.back() != '/') basepath += "/";

    if (isDirectory) {
      basepath += entry.substr(0, entry.length() - 1);
      if (!isRawBrowseFilesMode()) libraryView = 0;
      loadFiles();
      selectorIndex = 0;
      requestUpdate();
    } else {
      clearLibrarySearchSession(false);
      onSelectBook(basepath + entry);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lastNavigationInputMs = millis();
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (sortPreviewVisible) {
        sortPreviewVisible = false;
        requestUpdate();
        return;
      }
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        if (basepath == "/" && !isRawBrowseFilesMode()) libraryView = 0;
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else {
        if (isBookshelfMode() && !rawFilesLaunch && libraryView == LIBRARY_VIEW_FILES) {
          libraryView = LIBRARY_VIEW_DASHBOARD;
          selectorIndex = 0;
          loadFiles();
          clampSelector();
          requestUpdate();
          return;
        }
        if (isLibraryAuthorView()) {
          SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_AUTHOR;
          libraryFilterMode = LIBRARY_FILTER_ALL;
          libraryView = LIBRARY_VIEW_DASHBOARD;
          selectorIndex = libraryAuthorGroupSelectorIndex;
          saveLibraryRenderState();
          loadFiles();
          clampSelector();
          requestUpdate();
          return;
        }
        if (isBookshelfMode() && !rawFilesLaunch && libraryView != LIBRARY_VIEW_DASHBOARD) {
          libraryView = LIBRARY_VIEW_DASHBOARD;
          selectorIndex = 0;
          loadFiles();
          requestUpdate();
          return;
        }
        onGoHome();
      }
    }
  }

  int listSize = static_cast<int>(files.size());
  if (usesBookshelfGrid()) {
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { moveBookshelfHorizontal(1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { moveBookshelfHorizontal(-1); });
    buttonNavigator.onPress({MappedInputManager::Button::Down}, [this] { moveBookshelfVertical(1); });
    buttonNavigator.onPress({MappedInputManager::Button::Up}, [this] { moveBookshelfVertical(-1); });
    buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, pageItems] { moveBookshelfPage(1, pageItems); });
    buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, pageItems] { moveBookshelfPage(-1, pageItems); });
  } else {
    buttonNavigator.onNextPress([this, listSize] {
      lastNavigationInputMs = millis();
      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
      clampSelector();
      requestUpdate();
    });

    buttonNavigator.onPreviousPress([this, listSize] {
      lastNavigationInputMs = millis();
      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
      clampSelector();
      requestUpdate();
    });

    buttonNavigator.onNextContinuous([this, listSize, pageItems] {
      lastNavigationInputMs = millis();
      selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
      clampSelector();
      requestUpdate();
    });

    buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
      lastNavigationInputMs = millis();
      selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
      clampSelector();
      requestUpdate();
    });
  }

  if (!mappedInput.isAnyMappedButtonPressed() && millis() - lastNavigationInputMs >= LIBRARY_CACHED_COVER_IDLE_MS) {
    libraryWorkPaused = false;
  }
  if (!processLibraryIndexJob() && !libraryIndexingActive) {
    processVisibleCoverJob(pageItems);
  }
}

void LibraryActivity::openBookActions(const size_t index) {
  if (index >= files.size()) return;
  const std::string entry = files[index];
  if (!entry.empty() && entry.back() == '/') return;

  const std::string fullPath = index < entryPaths.size() && !entryPaths[index].empty() ? entryPaths[index]
                                                                                       : getFullPathForEntry(entry);
  if (fullPath.empty()) return;
  const std::string title = getEntryTitle(static_cast<int>(index));

  libraryOverlayBookPath = fullPath;
  libraryOverlayBookTitle = title;
  libraryOverlayBookEntry = entry;
  libraryOverlayMode = LIBRARY_OVERLAY_BOOK_ACTIONS;
  libraryOverlayIndex = 0;
  libraryOverlaySettingsChanged = false;
  confirmLongPressHandled = false;
  holdPreviewVisible = false;
  libraryWorkPaused = true;
  libraryCoverManager.cancelPrefetch();
  requestUpdate();
}

void LibraryActivity::openSortViewMenu() {
  libraryOverlayMode = LIBRARY_OVERLAY_MENU;
  libraryOverlayIndex = 0;
  libraryOverlaySettingsChanged = false;
  libraryWorkPaused = true;
  libraryCoverManager.cancelPrefetch();
  requestUpdate();
}

void LibraryActivity::openCollectionsMenu() {
  libraryOverlayMode = LIBRARY_OVERLAY_COLLECTIONS;
  libraryOverlayIndex = 0;
  libraryWorkPaused = true;
  libraryCoverManager.cancelPrefetch();
  requestUpdate();
}

std::vector<LibraryMenuItem> LibraryActivity::getLibraryOverlayItems() const {
  if (libraryOverlayMode == LIBRARY_OVERLAY_BOOK_ACTIONS) {
    const auto* statsBook = READING_STATS.findBook(libraryOverlayBookPath);
    const auto* metadata = LIBRARY_METADATA.findBook(libraryOverlayBookPath);
    const uint8_t state = classifyLibraryBookState(libraryOverlayBookPath, metadata, statsBook);
    const bool isToRead = state == LIBRARY_STATE_TO_READ;
    const bool isFinished = state == LIBRARY_STATE_FINISHED;
    (void)isToRead;
    const char* finishedLabel =
        I18N.get(isFinished ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED);
    const std::string stableId = BookIdentity::resolveStableBookId(libraryOverlayBookPath);
    const auto* manual = MANUAL_LIBRARY.findBook(libraryOverlayBookPath, stableId);
    const bool hasManualSeries = manual != nullptr && !manual->manualSeries.empty();
    const bool hasAnySeries = hasManualSeries || (metadata != nullptr && !metadata->seriesName.empty());
    const char* seriesLabel = I18N.get(hasAnySeries ? StrId::STR_CHANGE_SERIES : StrId::STR_ADD_TO_SERIES);
    std::vector<LibraryMenuItem> items = {
        {BOOK_ACTION_MARK_FINISHED, finishedLabel},
        {BOOK_ACTION_ADD_TO_SERIES, seriesLabel},
    };
    if (hasManualSeries) {
      items.push_back({BOOK_ACTION_REMOVE_FROM_SERIES, tr(STR_REMOVE_FROM_SERIES)});
    }
    items.push_back({BOOK_ACTION_EDIT_TITLE, tr(STR_EDIT_TITLE)});
    items.push_back({BOOK_ACTION_EDIT_AUTHOR, tr(STR_EDIT_AUTHOR)});
    items.push_back({BOOK_ACTION_EDIT_SERIES_NUMBER, tr(STR_EDIT_SERIES_NUMBER)});
    items.push_back({BOOK_ACTION_EDIT_TAGS, tr(STR_ADD_TAGS)});
    items.push_back({BOOK_ACTION_EDIT_RATING, tr(STR_ADD_RATING)});
    items.push_back({BOOK_ACTION_EDIT_NOTES, tr(STR_ADD_NOTES)});
    items.push_back({BOOK_ACTION_BOOK_INFO, tr(STR_BOOK_INFO)});
    items.push_back({BOOK_ACTION_REMOVE_STATE, tr(STR_REMOVE_READING_STATE)});
    items.push_back({BOOK_ACTION_DELETE, tr(STR_DELETE_BOOK)});
    return items;
  }
  if (libraryOverlayMode == LIBRARY_OVERLAY_COLLECTIONS) {
    return {{SORT_VIEW_COLLECTIONS_SERIES, tr(STR_SERIES)},
            {SORT_VIEW_COLLECTIONS_AUTHOR, tr(STR_AUTHOR)},
            {SORT_VIEW_COLLECTIONS_TO_READ, tr(STR_TO_READ)},
            {SORT_VIEW_COLLECTIONS_FINISHED, tr(STR_FINISHED_BOOKS)}};
  }
  if (libraryOverlayMode == LIBRARY_OVERLAY_SORT_BY) {
    return {{SORT_VIEW_SORT_TITLE, tr(STR_TITLE)},
            {SORT_VIEW_SORT_RECENT, tr(STR_RECENT_BOOKS)},
            {SORT_VIEW_SORT_PROGRESS, tr(STR_PROGRESS)}};
  }
  if (libraryOverlayMode == LIBRARY_OVERLAY_SORT_DIRECTION) {
    return {{SORT_VIEW_DIRECTION_ASC, tr(STR_SORT_ASC)}, {SORT_VIEW_DIRECTION_DESC, tr(STR_SORT_DESC)}};
  }
  return {{SORT_VIEW_SEARCH, tr(STR_SEARCH)},
          {SORT_VIEW_SORT, std::string(tr(STR_SORT_BY)) + ": " + librarySortLabel() + " >"},
          {SORT_VIEW_DIRECTION, std::string(tr(STR_SORT_DIRECTION)) + ": " + librarySortDirectionLabel() + " >"},
          {SORT_VIEW_COLLECTIONS, std::string(tr(STR_COLLECTIONS)) + " >"},
          {SORT_VIEW_REFRESH_COVERS, tr(STR_REFRESH_COVERS)},
          {SORT_VIEW_REFRESH_LIBRARY, tr(STR_REFRESH_LIBRARY)},
          {SORT_VIEW_RESET_LIBRARY, tr(STR_RESET_LIBRARY_INDEX)},
          {SORT_VIEW_BROWSE_FILES, tr(STR_BROWSE_ALL_FILES)}};
}

void LibraryActivity::closeLibraryOverlay(const bool redrawContent) {
  const bool changed = libraryOverlaySettingsChanged;
  libraryOverlayMode = LIBRARY_OVERLAY_NONE;
  libraryOverlayIndex = 0;
  libraryOverlaySettingsChanged = false;
  libraryOverlayBookPath.clear();
  libraryOverlayBookTitle.clear();
  libraryOverlayBookEntry.clear();
  libraryWorkPaused = false;
  mappedInput.consumeActiveHoldUntilRelease();
  if (redrawContent && !changed) {
    librarySelectionOnlyRender = false;
    libraryHasRetainedGridRects = false;
    libraryRetainedRenderToken.clear();
    requestUpdate();
    return;
  }
  if (changed) {
    libraryCoverManager.cancelPrefetch();
    const std::string selectedPath =
        selectorIndex < entryPaths.size() && !entryPaths[selectorIndex].empty() ? entryPaths[selectorIndex] : "";
    if (isLibraryAuthorView() &&
        (SETTINGS.librarySort != CrossPointSettings::LIBRARY_SORT_AUTHOR || libraryFilterMode != LIBRARY_FILTER_ALL)) {
      libraryView = LIBRARY_VIEW_DASHBOARD;
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      selectorIndex = 0;
    }
    if (!isLibraryAuthorView()) {
      if (libraryFilterMode == LIBRARY_FILTER_TO_READ) {
        libraryView = LIBRARY_VIEW_TO_READ;
      } else if (libraryFilterMode == LIBRARY_FILTER_FINISHED) {
        libraryView = LIBRARY_VIEW_FINISHED;
      } else {
        libraryView = LIBRARY_VIEW_DASHBOARD;
      }
    }
    if (libraryFilterMode != LIBRARY_FILTER_ALL || SETTINGS.librarySort != CrossPointSettings::LIBRARY_SORT_AUTHOR) {
      libraryAuthorGroupSnapshot() = {};
    }
    librarySelectionOnlyRender = false;
    libraryHasRetainedGridRects = false;
    libraryRetainedRenderToken.clear();
    loadFiles();
    if (isLibraryDashboard()) {
      sortLibraryDashboardBooks();
      if (libraryFilterMode == LIBRARY_FILTER_ALL && SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_AUTHOR) {
        saveLibraryAuthorGroupSnapshot();
      } else {
        saveLibraryDashboardSnapshot();
        saveLibraryDashboardIndex();
      }
    }
    if (!selectedPath.empty()) {
      for (size_t index = 0; index < entryPaths.size(); ++index) {
        if (entryPaths[index] == selectedPath) {
          selectorIndex = index;
          break;
        }
      }
    }
    clampSelector();
    saveLibraryRenderState();
    requestUpdate();
    return;
  }
}

void LibraryActivity::executeLibraryCollectionsAction(const int action) {
  closeLibraryOverlay(false);
  switch (static_cast<SortViewAction>(action)) {
    case SORT_VIEW_COLLECTIONS_AUTHOR:
      libraryCoverManager.cancelPrefetch();
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_AUTHOR;
      SETTINGS.saveToFile();
      libraryFilterMode = LIBRARY_FILTER_ALL;
      clearLibrarySearchSession(false);
      libraryView = LIBRARY_VIEW_DASHBOARD;
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      selectorIndex = 0;
      loadFiles();
      requestUpdate();
      return;
    case SORT_VIEW_COLLECTIONS_SERIES:
      openSeriesCollectionPicker();
      return;
    case SORT_VIEW_COLLECTIONS_TO_READ:
      libraryFilterMode = LIBRARY_FILTER_TO_READ;
      clearLibrarySearchSession(false);
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      libraryView = LIBRARY_VIEW_TO_READ;
      selectorIndex = 0;
      libraryCoverManager.cancelPrefetch();
      loadFiles();
      requestUpdate();
      return;
    case SORT_VIEW_COLLECTIONS_FINISHED:
      libraryFilterMode = LIBRARY_FILTER_FINISHED;
      clearLibrarySearchSession(false);
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      libraryView = LIBRARY_VIEW_FINISHED;
      selectorIndex = 0;
      libraryCoverManager.cancelPrefetch();
      loadFiles();
      requestUpdate();
      return;
    default:
      requestUpdate();
      return;
  }
}

void LibraryActivity::executeLibraryMenuAction(const int action) {
  switch (static_cast<SortViewAction>(action)) {
    case SORT_VIEW_SORT:
      libraryOverlayMode = LIBRARY_OVERLAY_SORT_BY;
      libraryOverlayIndex = 0;
      requestUpdate();
      return;
    case SORT_VIEW_SORT_TITLE:
      clearLibrarySearchSession(false);
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_TITLE;
      libraryFilterMode = LIBRARY_FILTER_ALL;
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      librarySeriesViewName.clear();
      libraryView = LIBRARY_VIEW_DASHBOARD;
      selectorIndex = 0;
      SETTINGS.saveToFile();
      libraryOverlaySettingsChanged = true;
      closeLibraryOverlay(true);
      return;
    case SORT_VIEW_SORT_RECENT:
      clearLibrarySearchSession(false);
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_RECENT;
      libraryFilterMode = LIBRARY_FILTER_ALL;
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      librarySeriesViewName.clear();
      libraryView = LIBRARY_VIEW_DASHBOARD;
      selectorIndex = 0;
      SETTINGS.saveToFile();
      libraryOverlaySettingsChanged = true;
      closeLibraryOverlay(true);
      return;
    case SORT_VIEW_SORT_PROGRESS:
      clearLibrarySearchSession(false);
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_PROGRESS;
      libraryFilterMode = LIBRARY_FILTER_ALL;
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      librarySeriesViewName.clear();
      libraryView = LIBRARY_VIEW_DASHBOARD;
      selectorIndex = 0;
      SETTINGS.saveToFile();
      libraryOverlaySettingsChanged = true;
      closeLibraryOverlay(true);
      return;
    case SORT_VIEW_DIRECTION:
      libraryOverlayMode = LIBRARY_OVERLAY_SORT_DIRECTION;
      libraryOverlayIndex = 0;
      requestUpdate();
      return;
    case SORT_VIEW_DIRECTION_ASC:
      clearLibrarySearchSession(false);
      librarySortDescending = false;
      SETTINGS.librarySortDescending = 0;
      SETTINGS.saveToFile();
      libraryOverlaySettingsChanged = true;
      requestUpdate();
      return;
    case SORT_VIEW_DIRECTION_DESC:
      clearLibrarySearchSession(false);
      librarySortDescending = true;
      SETTINGS.librarySortDescending = 1;
      SETTINGS.saveToFile();
      libraryOverlaySettingsChanged = true;
      requestUpdate();
      return;
    case SORT_VIEW_COLUMNS:
      SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_LAYOUT_3X3;
      SETTINGS.saveToFile();
      libraryOverlaySettingsChanged = true;
      requestUpdate();
      return;
    case SORT_VIEW_FILTER:
      libraryFilterMode = LIBRARY_FILTER_ALL;
      libraryOverlaySettingsChanged = true;
      requestUpdate();
      return;
    case SORT_VIEW_TO_READ_SHELF:
      SETTINGS.showToReadShelf = !SETTINGS.showToReadShelf;
      SETTINGS.saveToFile();
      libraryOverlaySettingsChanged = true;
      requestUpdate();
      return;
    case SORT_VIEW_COLLECTIONS:
      libraryOverlayMode = LIBRARY_OVERLAY_COLLECTIONS;
      libraryOverlayIndex = 0;
      requestUpdate();
      return;
    case SORT_VIEW_BROWSE_FILES:
      closeLibraryOverlay(false);
      clearLibraryBreadcrumb();
      activityManager.goToFileBrowser();
      return;
    case SORT_VIEW_SEARCH:
      closeLibraryOverlay(false);
      libraryCoverManager.cancelPrefetch();
      beginLibrarySearchSession();
      startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH),
                                                                     librarySearchQuery, 48),
                             [this](const ActivityResult& searchResult) {
                               mappedInput.consumeActiveHoldUntilRelease();
                               if (!searchResult.isCancelled &&
                                   std::holds_alternative<KeyboardResult>(searchResult.data)) {
                                 librarySearchQuery = std::get<KeyboardResult>(searchResult.data).text;
                                 if (librarySearchQuery.empty()) {
                                   clearLibrarySearchSession(true);
                                   requestUpdate(true);
                                   return;
                                 }
                                 selectorIndex = 0;
                                 librarySelectionOnlyRender = false;
                                 libraryHasRetainedGridRects = false;
                                 libraryRetainedRenderToken.clear();
                                 libraryCoverManager.cancelPrefetch();
                               } else {
                                 clearLibrarySearchSession(true);
                               }
                               requestUpdate(true);
                             });
      return;
    case SORT_VIEW_REFRESH_LIBRARY:
      closeLibraryOverlay(false);
      libraryCoverManager.cancelPrefetch();
      libraryProgressAction = LIBRARY_PROGRESS_REFRESH;
      libraryFilterMode = LIBRARY_FILTER_ALL;
      clearLibrarySearchSession(false);
      libraryAuthorViewKey.clear();
      libraryAuthorViewName.clear();
      libraryView = LIBRARY_VIEW_DASHBOARD;
      libraryScanRequested = true;
      libraryWorkPaused = false;
      libraryIndexingActive = false;
      libraryScanFolders.clear();
      libraryScanOffsets.clear();
      libraryCurrentScanFolder.clear();
      librarySkippedFolderName.clear();
      loadFiles();
      clampSelector();
      requestUpdate(true);
      return;
    case SORT_VIEW_RESET_LIBRARY:
      closeLibraryOverlay(false);
      libraryCoverManager.cancelPrefetch();
      libraryProgressAction = LIBRARY_PROGRESS_REBUILD;
      libraryFilterMode = LIBRARY_FILTER_ALL;
      clearLibrarySearchSession(false);
      libraryView = LIBRARY_VIEW_DASHBOARD;
      rawFilesLaunch = false;
      selectorIndex = 0;
      resetLibraryDashboardState(true);
      libraryScanRequested = true;
      loadFiles();
      clampSelector();
      requestUpdate(true);
      return;
    case SORT_VIEW_REFRESH_COVERS:
      closeLibraryOverlay(false);
      libraryCoverManager.cancelPrefetch();
      libraryCoverManager.clearWindows();
      libraryProgressAction = LIBRARY_PROGRESS_COVERS;
      libraryPostIndexCoverWarmup = false;
      libraryCoverWarmupPending = false;
      libraryCoverWarmupTotal = 0;
      libraryCoverWarmupDone = 0;
      libraryCurrentCoverTitle.clear();
      libraryCoverWarmupQueueCount = 0;
      libraryBadPaths.clear();
      libraryFailureCount = 0;
      for (auto& record : thumbnailCache()) {
        if (record.state == ENTRY_COVER_MISSING) {
          record.state = ENTRY_COVER_UNKNOWN;
          record.thumbPath.clear();
        }
      }
      for (size_t index = 0; index < entryCoverStates.size(); ++index) {
        if (entryTypes.size() > index && entryTypes[index] == ENTRY_TYPE_BOOK &&
            entryCoverStates[index] == ENTRY_COVER_MISSING) {
          entryCoverStates[index] = ENTRY_COVER_UNKNOWN;
        }
      }
      {
        const int visibleItems = std::max(1, std::min(MAX_LIBRARY_ACTIVE_COVERS, getPageItems(libraryRetainedContentHeight)));
        auto resetVisibleCover = [this](const int index) {
          if (index < 0 || index >= static_cast<int>(entryCoverStates.size()) ||
              index >= static_cast<int>(entryTypes.size()) || entryTypes[index] != ENTRY_TYPE_BOOK ||
              index >= static_cast<int>(entryPaths.size()) || !FsHelpers::hasEpubExtension(entryPaths[index])) {
            return;
          }
          entryCoverStates[index] = ENTRY_COVER_UNKNOWN;
          if (index < static_cast<int>(entryCoverPaths.size())) {
            entryCoverPaths[index].clear();
          }
        };
        if (isLibraryDashboard()) {
          const auto visibleIndices = getVisibleDashboardIndices();
          const int currentPosition = getVisibleDashboardPosition(visibleIndices);
          const int pageStart = currentPosition >= 0 ? (currentPosition / visibleItems) * visibleItems : 0;
          const int pageEnd = std::min(static_cast<int>(visibleIndices.size()), pageStart + visibleItems);
          for (int position = pageStart; position < pageEnd; ++position) {
            resetVisibleCover(visibleIndices[position]);
          }
        } else {
          const int pageStart = (static_cast<int>(selectorIndex) / visibleItems) * visibleItems;
          const int pageEnd = std::min(static_cast<int>(files.size()), pageStart + visibleItems);
          for (int index = pageStart; index < pageEnd; ++index) {
            resetVisibleCover(index);
          }
        }
      }
      requestUpdate(true);
      return;
    default:
      closeLibraryOverlay(true);
      return;
  }
}

bool LibraryActivity::handleLibraryOverlayInput() {
  if (libraryOverlayMode == LIBRARY_OVERLAY_NONE) return false;
  const auto items = getLibraryOverlayItems();
  if (items.empty()) {
    closeLibraryOverlay(true);
    return true;
  }
  const int count = static_cast<int>(items.size());
  if (libraryOverlayIndex >= count) libraryOverlayIndex = count - 1;
  if (libraryOverlayIndex < 0) libraryOverlayIndex = 0;
  buttonNavigator.onNext([this, count] {
    libraryOverlayIndex = ButtonNavigator::nextIndex(libraryOverlayIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    libraryOverlayIndex = ButtonNavigator::previousIndex(libraryOverlayIndex, count);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int action = items[libraryOverlayIndex].action;
    if (libraryOverlayMode == LIBRARY_OVERLAY_BOOK_ACTIONS) {
      const std::string path = libraryOverlayBookPath;
      const std::string title = libraryOverlayBookTitle;
      const std::string entry = libraryOverlayBookEntry;
      closeLibraryOverlay(false);
      handleBookAction(action, path, title, entry);
    } else if (libraryOverlayMode == LIBRARY_OVERLAY_COLLECTIONS) {
      executeLibraryCollectionsAction(action);
    } else {
      executeLibraryMenuAction(action);
    }
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (libraryOverlayMode == LIBRARY_OVERLAY_COLLECTIONS || libraryOverlayMode == LIBRARY_OVERLAY_SORT_BY ||
        libraryOverlayMode == LIBRARY_OVERLAY_SORT_DIRECTION) {
      libraryOverlayMode = LIBRARY_OVERLAY_MENU;
      libraryOverlayIndex = 0;
      requestUpdate();
    } else {
      closeLibraryOverlay(true);
    }
    return true;
  }
  return true;
}

void LibraryActivity::renderLibraryOverlay(const int pageWidth, const int pageHeight) {
  const auto items = getLibraryOverlayItems();
  if (items.empty()) return;
  std::vector<std::string> context;
  if (libraryOverlayMode == LIBRARY_OVERLAY_BOOK_ACTIONS) {
    if (!libraryOverlayBookTitle.empty()) {
      context.push_back(libraryOverlayBookTitle);
    }
    const auto selectedIt = std::find(entryPaths.begin(), entryPaths.end(), libraryOverlayBookPath);
    if (selectedIt != entryPaths.end()) {
      const int entryIndex = static_cast<int>(std::distance(entryPaths.begin(), selectedIt));
      const std::string subtitle = getEntrySubtitle(entryIndex);
      if (!subtitle.empty()) {
        context.push_back(subtitle);
      }
    }
  }

  std::string title = tr(STR_SORT_VIEW);
  if (libraryOverlayMode == LIBRARY_OVERLAY_COLLECTIONS) {
    title = tr(STR_COLLECTIONS);
  } else if (libraryOverlayMode == LIBRARY_OVERLAY_SORT_BY) {
    title = tr(STR_SORT_BY);
  } else if (libraryOverlayMode == LIBRARY_OVERLAY_SORT_DIRECTION) {
    title = tr(STR_SORT_DIRECTION);
  } else if (libraryOverlayMode == LIBRARY_OVERLAY_BOOK_ACTIONS) {
    title = tr(STR_BOOK_ACTIONS);
  }
  std::vector<CompactHudRenderer::Row> rows;
  rows.reserve(items.size());
  for (const auto& item : items) {
    rows.push_back(CompactHudRenderer::Row{item.label, "", false});
  }
  CompactHudRenderer::ActionListConfig config;
  config.title = title;
  config.context = std::move(context);
  config.rows = std::move(rows);
  config.selectedIndex = libraryOverlayIndex;
  config.minWidth = 350;
  config.maxRows = 8;
  CompactHudRenderer::drawActionList(renderer, mappedInput, config);
}

std::vector<std::string> LibraryActivity::getLibrarySeriesNames() {
  LibraryManualSeriesStore store;
  store.load();
  std::vector<std::string> names = store.names();
  std::set<std::string> seen;
  for (const auto& name : names) {
    const std::string key = lowercaseCopy(trimCopy(name));
    if (!key.empty()) seen.insert(key);
  }

  if (!libraryDashboardSnapshot().valid && entryPaths.empty()) {
    restoreLibraryDashboardIndex();
  }
  const auto sourcePaths = libraryDashboardSnapshot().valid ? libraryDashboardSnapshot().entryPaths : entryPaths;
  for (const auto& path : sourcePaths) {
    if (path.empty()) continue;
    const std::string stableId = BookIdentity::resolveStableBookId(path);
    const auto* metadata = BOOK_METADATA.findBook(path, stableId);
    if (!metadata || metadata->series.empty()) continue;
    const std::string seriesName = trimCopy(metadata->series);
    const std::string key = lowercaseCopy(seriesName);
    if (key.empty() || seen.count(key) > 0) continue;
    seen.insert(key);
    names.push_back(seriesName);
  }
  std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) { return naturalLess(a, b); });
  return names;
}

void LibraryActivity::openSeriesCollectionPicker() {
  startActivityForResult(std::make_unique<SeriesPickerActivity>(renderer, mappedInput, getLibrarySeriesNames()),
                         [this](const ActivityResult& result) {
                           mappedInput.consumeActiveHoldUntilRelease();
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
      if (std::holds_alternative<KeyboardResult>(result.data)) {
                             librarySeriesViewName = std::get<KeyboardResult>(result.data).text;
                             clearLibrarySearchSession(false);
                             libraryFilterMode = LIBRARY_FILTER_ALL;
                             libraryAuthorViewKey.clear();
                             libraryAuthorViewName.clear();
                             libraryView = LIBRARY_VIEW_SERIES;
                             selectorIndex = 0;
                             libraryCoverManager.cancelPrefetch();
                             loadFiles();
                             saveLibraryRenderState();
                             requestUpdate(true);
                             return;
                           }
                           requestUpdate();
                         });
}

void LibraryActivity::handleBookAction(const int action, const std::string& path, const std::string& title,
                                           const std::string& entry) {
  std::string bookId;
  if (const auto* metadata = LIBRARY_METADATA.findBook(path)) {
    bookId = !metadata->stableId.empty() ? metadata->stableId : metadata->bookId;
  }
  if (bookId.empty() && !path.empty()) {
    bookId = BookIdentity::resolveStableBookId(path);
  }
  const std::string bookKey = !bookId.empty() ? bookId : path;

  switch (static_cast<BookAction>(action)) {
    case BOOK_ACTION_CONTINUE:
      clearLibrarySearchSession(false);
      onSelectBook(path);
      return;
    case BOOK_ACTION_MARK_TO_READ:
      if (LIBRARY_METADATA.isToRead(path)) {
        LIBRARY_METADATA.removeFromToRead(path);
      } else {
        LIBRARY_METADATA.setToRead(path);
      }
      if (isLibraryDashboard()) {
        for (size_t index = 0; index < entryPaths.size(); ++index) {
          if (entryPaths[index] == path && index < libraryFileStates.size()) {
            libraryFileStates[index] =
                LIBRARY_METADATA.isToRead(path) ? LIBRARY_STATE_TO_READ : LIBRARY_STATE_UNREAD;
            completedFileStates[index] = 0;
            break;
          }
        }
        sortLibraryDashboardBooks();
        saveLibraryDashboardIndex();
      } else {
        loadFiles();
      }
      requestUpdate(true);
      return;
    case BOOK_ACTION_MARK_FINISHED:
      if (LIBRARY_METADATA.isFinished(bookKey) || LIBRARY_METADATA.isFinished(path)) {
        LIBRARY_METADATA.removeFinishedState(path, bookId);
      } else {
        LIBRARY_METADATA.setFinished(path, bookId);
        if (SETTINGS.removeReadBooksFromRecents) {
          RECENT_BOOKS.removeBook(bookKey);
          RECENT_BOOKS.removeBook(path);
        }
      }
      if (isLibraryDashboard()) {
        for (size_t index = 0; index < entryPaths.size(); ++index) {
          if (entryPaths[index] == path && index < libraryFileStates.size()) {
            const bool finished = LIBRARY_METADATA.isFinished(bookKey) || LIBRARY_METADATA.isFinished(path);
            libraryFileStates[index] = finished ? LIBRARY_STATE_FINISHED : LIBRARY_STATE_UNREAD;
            if (index < completedFileStates.size()) {
              completedFileStates[index] = finished ? 1 : 0;
            }
            if (index < resolvedRows.size()) {
              resolvedRows[index].finished = finished;
              resolvedRows[index].state = libraryFileStates[index];
            }
            break;
          }
        }
        sortLibraryDashboardBooks();
        saveLibraryDashboardIndex();
      } else {
        loadFiles();
      }
      clampSelector();
      requestUpdate(true);
      return;
    case BOOK_ACTION_REMOVE_STATE:
      LIBRARY_METADATA.removeActiveReadingState(path);
      loadFiles();
      requestUpdate(true);
      return;
    case BOOK_ACTION_ADD_TO_SERIES:
      openSeriesPicker(path, title);
      return;
    case BOOK_ACTION_REMOVE_FROM_SERIES:
      {
        LibraryManualSeriesStore store;
        store.load();
        store.removeBook(bookId, path);
        MANUAL_LIBRARY.setManualSeries(path, "", "", bookId);
        loadFiles();
        clampSelector();
        requestUpdate(true);
      }
      return;
    case BOOK_ACTION_EDIT_TITLE:
      startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TITLE), title, 96),
                             [this, path](const ActivityResult& keyboardResult) {
                               mappedInput.consumeActiveHoldUntilRelease();
                               if (!keyboardResult.isCancelled &&
                                   std::holds_alternative<KeyboardResult>(keyboardResult.data)) {
                                 const std::string stableId = BookIdentity::resolveStableBookId(path);
                                 MANUAL_LIBRARY.setManualTitle(path, std::get<KeyboardResult>(keyboardResult.data).text,
                                                               stableId);
                                 loadFiles();
                                 requestUpdate(true);
                                 return;
                               }
                               requestUpdate();
                             });
      return;
    case BOOK_ACTION_EDIT_AUTHOR:
      startActivityForResult(
          std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_AUTHOR), getEntrySubtitle(selectorIndex),
                                                 96),
          [this, path](const ActivityResult& keyboardResult) {
            mappedInput.consumeActiveHoldUntilRelease();
            if (!keyboardResult.isCancelled && std::holds_alternative<KeyboardResult>(keyboardResult.data)) {
              const std::string stableId = BookIdentity::resolveStableBookId(path);
              MANUAL_LIBRARY.setManualAuthor(path, std::get<KeyboardResult>(keyboardResult.data).text, stableId);
              loadFiles();
              requestUpdate(true);
              return;
            }
            requestUpdate();
          });
      return;
    case BOOK_ACTION_EDIT_TAGS:
      {
        const std::string stableId = BookIdentity::resolveStableBookId(path);
        std::string initialTags;
        if (const auto* manual = MANUAL_LIBRARY.findBook(path, stableId)) {
          initialTags = joinLibraryValues(manual->personalTags);
        }
        startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TAGS), initialTags,
                                                                       128),
                               [this, path](const ActivityResult& keyboardResult) {
                                 mappedInput.consumeActiveHoldUntilRelease();
                                 if (!keyboardResult.isCancelled &&
                                     std::holds_alternative<KeyboardResult>(keyboardResult.data)) {
                                   const std::string stableId = BookIdentity::resolveStableBookId(path);
                                   MANUAL_LIBRARY.setPersonalTags(
                                       path, std::get<KeyboardResult>(keyboardResult.data).text, stableId);
                                   loadFiles();
                                   requestUpdate(true);
                                   return;
                                 }
                                 requestUpdate();
                               });
      }
      return;
    case BOOK_ACTION_EDIT_RATING:
      {
        const std::string stableId = BookIdentity::resolveStableBookId(path);
        std::string initialRating;
        if (const auto* manual = MANUAL_LIBRARY.findBook(path, stableId); manual != nullptr && manual->rating > 0) {
          initialRating = std::to_string(manual->rating);
        }
        startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RATING),
                                                                       initialRating, 1),
                               [this, path](const ActivityResult& keyboardResult) {
                                 mappedInput.consumeActiveHoldUntilRelease();
                                 if (!keyboardResult.isCancelled &&
                                     std::holds_alternative<KeyboardResult>(keyboardResult.data)) {
                                   const auto& text = std::get<KeyboardResult>(keyboardResult.data).text;
                                   const int parsed = text.empty() ? 0 : std::atoi(text.c_str());
                                   const std::string stableId = BookIdentity::resolveStableBookId(path);
                                   MANUAL_LIBRARY.setRating(path, static_cast<uint8_t>(std::clamp(parsed, 0, 5)),
                                                            stableId);
                                   loadFiles();
                                   requestUpdate(true);
                                   return;
                                 }
                                 requestUpdate();
                               });
      }
      return;
    case BOOK_ACTION_EDIT_NOTES:
      {
        const std::string stableId = BookIdentity::resolveStableBookId(path);
        std::string initialNotes;
        if (const auto* manual = MANUAL_LIBRARY.findBook(path, stableId)) {
          initialNotes = manual->notes;
        }
        startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NOTES),
                                                                       initialNotes, 240),
                               [this, path](const ActivityResult& keyboardResult) {
                                 mappedInput.consumeActiveHoldUntilRelease();
                                 if (!keyboardResult.isCancelled &&
                                     std::holds_alternative<KeyboardResult>(keyboardResult.data)) {
                                   const std::string stableId = BookIdentity::resolveStableBookId(path);
                                   MANUAL_LIBRARY.setNotes(path, std::get<KeyboardResult>(keyboardResult.data).text,
                                                           stableId);
                                   loadFiles();
                                   requestUpdate(true);
                                   return;
                                 }
                                 requestUpdate();
                               });
      }
      return;
    case BOOK_ACTION_EDIT_SERIES_NUMBER:
      {
        const std::string stableId = BookIdentity::resolveStableBookId(path);
        std::string initialNumber;
        std::string seriesName;
        if (const auto* manual = MANUAL_LIBRARY.findBook(path, stableId)) {
          initialNumber = manual->manualSeriesNumber;
          seriesName = manual->manualSeries;
        }
        if (const auto* metadata = LIBRARY_METADATA.findBook(!stableId.empty() ? stableId : path)) {
          if (seriesName.empty()) seriesName = metadata->seriesName;
          if (initialNumber.empty()) {
            initialNumber = !metadata->seriesNumber.empty() ? metadata->seriesNumber : metadata->seriesIndex;
          }
        }
        startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SERIES_INDEX),
                                                                       initialNumber, 12),
                               [this, path, seriesName](const ActivityResult& keyboardResult) {
                                 mappedInput.consumeActiveHoldUntilRelease();
                                 if (!keyboardResult.isCancelled &&
                                     std::holds_alternative<KeyboardResult>(keyboardResult.data)) {
                                   const std::string stableId = BookIdentity::resolveStableBookId(path);
                                   MANUAL_LIBRARY.setManualSeries(path, seriesName,
                                                                  std::get<KeyboardResult>(keyboardResult.data).text,
                                                                  stableId);
                                   loadFiles();
                                   requestUpdate(true);
                                   return;
                                 }
                                 requestUpdate();
                               });
      }
      return;
    case BOOK_ACTION_BOOK_INFO:
      startActivityForResult(
          std::make_unique<ReaderBookInfoActivity>(renderer, mappedInput, path, title,
                                                   getEntrySubtitle(static_cast<int>(selectorIndex))),
          [this](const ActivityResult&) {
            mappedInput.consumeActiveHoldUntilRelease();
            requestUpdate(true);
          });
      return;
    case BOOK_ACTION_DELETE:
      confirmDeleteFile(path, title.empty() ? entry : title);
      return;
  }
}

void LibraryActivity::openSeriesPicker(const std::string& path, const std::string& title) {
  LibraryManualSeriesStore store;
  store.load();
  startActivityForResult(std::make_unique<SeriesPickerActivity>(renderer, mappedInput, store.names()),
                         [this, path, title](const ActivityResult& result) {
                           mappedInput.consumeActiveHoldUntilRelease();
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           if (std::holds_alternative<KeyboardResult>(result.data)) {
                             assignBookToSeries(path, std::get<KeyboardResult>(result.data).text);
                             requestUpdate(true);
                             return;
                           }
                           if (std::holds_alternative<MenuResult>(result.data) &&
                               std::get<MenuResult>(result.data).action == SeriesPickerActivity::createNewAction()) {
                             startActivityForResult(
                                 std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SERIES_NAME),
                                                                        title, 48),
                                 [this, path](const ActivityResult& keyboardResult) {
                                   mappedInput.consumeActiveHoldUntilRelease();
                                   if (!keyboardResult.isCancelled &&
                                       std::holds_alternative<KeyboardResult>(keyboardResult.data)) {
                                     assignBookToSeries(path, std::get<KeyboardResult>(keyboardResult.data).text);
                                     requestUpdate(true);
                                     return;
                                   }
                                   requestUpdate();
                                 });
                             return;
                           }
                           requestUpdate();
                         });
}

void LibraryActivity::assignBookToSeries(const std::string& path, const std::string& seriesName) {
  if (path.empty() || seriesName.empty()) return;
  LibraryManualSeriesStore store;
  store.load();
  const std::string stableId = BookIdentity::resolveStableBookId(path);
  store.assignBook(seriesName, stableId, path);
  MANUAL_LIBRARY.setManualSeries(path, seriesName, "", stableId);
}

void LibraryActivity::confirmDeleteFile(const std::string& fullPath, const std::string& label) {
  auto handler = [this, fullPath](const ActivityResult& res) {
    if (!res.isCancelled) {
      LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
      clearFileMetadata(fullPath);
      if (Storage.remove(fullPath.c_str())) {
        LOG_DBG("FileBrowser", "Deleted successfully");
        LIBRARY_METADATA.clearReadingState(fullPath);
        READING_STATS.removeBook(fullPath);
        RECENT_BOOKS.removeBook(fullPath);
        loadFiles();
        clampSelector();
        requestUpdate(true);
      } else {
        LOG_ERR("FileBrowser", "Failed to delete file: %s", fullPath.c_str());
        requestUpdate(true);
      }
    } else {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      requestUpdate();
    }
  };

  std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, label), handler);
}

std::string getFileName(std::string filename) {
  if (filename.empty()) return "";
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return pos == std::string::npos ? filename : filename.substr(0, pos);
}

std::string getFileExtension(std::string filename) {
  if (filename.empty()) return "";
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return pos == std::string::npos ? "" : filename.substr(pos);
}

bool LibraryActivity::isBookshelfMode() const {
  return SETTINGS.uiTheme == CrossPointSettings::LYRA_VCODEX2 &&
         SETTINGS.fileBrowserView == CrossPointSettings::FILE_BROWSER_BOOKSHELF;
}

int LibraryActivity::getBookshelfColumns() const {
  return 3;
}

int LibraryActivity::getBookshelfRows() const {
  return 3;
}

int LibraryActivity::getBookshelfCardHeight() const {
  return 132;
}

int LibraryActivity::getPageItems(const int contentHeight) const {
  if (usesBookshelfGrid()) {
    const int requestedRows = getBookshelfRows();
    const int requestedItems = std::max(1, requestedRows * getBookshelfColumns());
    const int rows = std::max(1, std::min(requestedRows, (contentHeight + BOOKSHELF_CARD_GAP) /
                                                         std::max(1, SHELF_COVER_MIN_HEIGHT + BOOKSHELF_CARD_GAP)));
    const int visibleItems = std::min(requestedItems, rows * getBookshelfColumns());
    if (isLibraryShelf()) return visibleItems;
    if (basepath != "/") return visibleItems;
    return visibleItems;
  }
  const int reserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  return UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, reserved);
}

std::vector<int> LibraryActivity::getVisibleDashboardIndices() const {
  std::vector<int> indices;
  indices.reserve(files.size());
  auto matchesSearch = [this](const int index) {
    if (librarySearchQuery.empty()) return true;
    const std::string title = getEntryTitle(index);
    const std::string text =
        index >= 0 && index < static_cast<int>(resolvedRows.size()) ? resolvedRows[index].searchText : getEntrySubtitle(index);
    return LibrarySearch::matches(title, text, librarySearchQuery);
  };
  if (!isLibraryDashboard()) {
    for (int index = 0; index < static_cast<int>(files.size()); ++index) {
      if (!matchesSearch(index)) continue;
      indices.push_back(index);
    }
    return indices;
  }

  const int toReadPreviewLimit = 3;
  int toReadPreviewCount = 0;
  if (libraryFilterMode == LIBRARY_FILTER_ALL && SETTINGS.showToReadShelf) {
    for (int index = 0; index < static_cast<int>(files.size()) && toReadPreviewCount < toReadPreviewLimit; ++index) {
      const uint8_t state = index < static_cast<int>(libraryFileStates.size()) ? libraryFileStates[index] : 0;
      if (state == LIBRARY_STATE_TO_READ) {
        if (!matchesSearch(index)) continue;
        indices.push_back(index);
        ++toReadPreviewCount;
      }
    }
    if (toReadPreviewCount > 0) {
      while (toReadPreviewCount < toReadPreviewLimit) {
        indices.push_back(-1);
        ++toReadPreviewCount;
      }
    }
  }

  for (int index = 0; index < static_cast<int>(files.size()); ++index) {
    const uint8_t state = index < static_cast<int>(libraryFileStates.size()) ? libraryFileStates[index] : 0;
    if (libraryFilterMode == LIBRARY_FILTER_ALL && state == LIBRARY_STATE_TO_READ) continue;
    if (!matchesSearch(index)) continue;
    indices.push_back(index);
  }
  return indices;
}

int LibraryActivity::getVisibleDashboardPosition(const std::vector<int>& visibleIndices) const {
  if (visibleIndices.empty()) return 0;
  for (int position = 0; position < static_cast<int>(visibleIndices.size()); ++position) {
    if (visibleIndices[position] == static_cast<int>(selectorIndex)) {
      return position;
    }
  }
  for (int position = 0; position < static_cast<int>(visibleIndices.size()); ++position) {
    if (visibleIndices[position] >= 0) return position;
  }
  return 0;
}

uint16_t LibraryActivity::countFolderItems(const std::string& folderName) const {
  if (folderName.empty() || folderName.back() != '/') return 0;

  std::string path = basepath;
  if (path.empty() || path.back() != '/') path += "/";
  path += folderName.substr(0, folderName.length() - 1);

  auto dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  uint16_t count = 0;
  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    std::string_view filename{name};
    const bool hidden = (!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0;
    const bool visibleFile = FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                             FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                             FsHelpers::hasBmpExtension(filename);
    if (!hidden && (file.isDirectory() || visibleFile) && count < 65535) {
      ++count;
    }
    file.close();
  }
  dir.close();
  return count;
}

std::string LibraryActivity::getFullPathForEntry(const std::string& entry) const {
  std::string path = basepath;
  if (path.empty() || path.back() != '/') path += "/";
  return path + entry;
}

std::string LibraryActivity::getLibraryStateLabel(const int index) const {
  if (index < 0 || index >= static_cast<int>(libraryFileStates.size())) {
    return "";
  }
  if (index < static_cast<int>(entryTypes.size()) && entryTypes[index] == ENTRY_TYPE_AUTHOR_GROUP) {
    return tr(STR_OPEN_AUTHOR);
  }

  switch (libraryFileStates[index]) {
    case LIBRARY_STATE_PINNED:
      return tr(STR_PINNED);
    case LIBRARY_STATE_TO_READ:
      return tr(STR_TO_READ);
    case LIBRARY_STATE_FINISHED:
      return tr(STR_DONE);
    case LIBRARY_STATE_READING: {
      const uint8_t progress =
          index < static_cast<int>(progressFileStates.size()) ? progressFileStates[index] : 0;
      return std::to_string(progress) + "%";
    }
    default:
      return "";
  }
}

std::string LibraryActivity::getEntryTitle(const int index) const {
  if (index >= 0 && index < static_cast<int>(resolvedRows.size()) && !resolvedRows[index].title.empty()) {
    return resolvedRows[index].title;
  }
  if (index >= 0 && index < static_cast<int>(entryTitles.size()) && !entryTitles[index].empty()) {
    return entryTitles[index];
  }
  if (index >= 0 && index < static_cast<int>(files.size())) {
    return getFileName(files[index]);
  }
  return "";
}

std::string LibraryActivity::getEntrySubtitle(const int index) const {
  if (index >= 0 && index < static_cast<int>(resolvedRows.size()) && !resolvedRows[index].author.empty()) {
    return resolvedRows[index].author;
  }
  if (index >= 0 && index < static_cast<int>(entrySubtitles.size())) {
    if (!entrySubtitles[index].empty()) {
      return entrySubtitles[index];
    }
    if (index < static_cast<int>(entryPaths.size()) && !entryPaths[index].empty()) {
      return authorDisplayName("", entryPaths[index]);
    }
  }
  return "";
}

std::string LibraryActivity::buildLibraryRetainedRenderToken(const int contentHeight) const {
  if (!usesBookshelfGrid() || contentHeight <= 0 || files.empty()) {
    return "";
  }
  const int pageItems = getPageItems(contentHeight);
  if (pageItems <= 0) {
    return "";
  }

  std::string token;
  token.reserve(256);
  token += "v=" + std::to_string(libraryView);
  token += "|f=" + std::to_string(libraryFilterMode);
  token += "|s=" + std::to_string(SETTINGS.librarySort);
  token += "|d=" + std::to_string(librarySortDescending ? 1 : 0);
  token += "|l=" + std::to_string(static_cast<int>(CrossPointSettings::BOOKSHELF_LAYOUT_3X3));
  token += "|r=" + std::to_string(SETTINGS.showToReadShelf ? 1 : 0);
  token += "|a=" + libraryAuthorViewKey;
  token += "|h=" + std::to_string(contentHeight);

  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) {
      return token + "|empty";
    }
    const int visiblePosition = getVisibleDashboardPosition(visibleIndices);
    const int pageStart = (visiblePosition / pageItems) * pageItems;
    const int pageEnd = std::min(static_cast<int>(visibleIndices.size()), pageStart + pageItems);
    token += "|p=" + std::to_string(pageStart / pageItems);
    for (int position = pageStart; position < pageEnd; ++position) {
      const int index = visibleIndices[position];
      token += "|i=" + std::to_string(index);
      if (index >= 0 && index < static_cast<int>(entryTypes.size())) {
        token += ":" + std::to_string(entryTypes[index]);
      }
      if (index >= 0 && index < static_cast<int>(entryPaths.size())) {
        token += ":" + entryPaths[index];
      }
      if (index >= 0 && index < static_cast<int>(entryCoverPaths.size())) {
        token += ":" + entryCoverPaths[index];
      }
      if (index >= 0 && index < static_cast<int>(entryCoverStates.size())) {
        token += ":" + std::to_string(entryCoverStates[index]);
      }
    }
    return token;
  }

  const int pageStart = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEnd = std::min(static_cast<int>(files.size()), pageStart + pageItems);
  token += "|p=" + std::to_string(pageStart / pageItems);
  for (int index = pageStart; index < pageEnd; ++index) {
    token += "|i=" + std::to_string(index) + ":" + files[index];
    if (index >= 0 && index < static_cast<int>(entryCoverPaths.size())) {
      token += ":" + entryCoverPaths[index];
    }
    if (index >= 0 && index < static_cast<int>(entryCoverStates.size())) {
      token += ":" + std::to_string(entryCoverStates[index]);
    }
  }
  return token;
}

void LibraryActivity::moveBookshelfHorizontal(const int delta) {
  if (files.empty()) return;
  lastNavigationInputMs = millis();
  libraryWorkPaused = true;
  libraryDirtyCoverIndex = -1;
  libraryPreviousSelectorIndex = selectorIndex;
  librarySelectionOnlyRender = true;
  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) return;
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    int nextPosition = currentPosition;
    for (int step = 0; step < static_cast<int>(visibleIndices.size()); ++step) {
      nextPosition = delta > 0 ? ButtonNavigator::nextIndex(nextPosition, static_cast<int>(visibleIndices.size()))
                               : ButtonNavigator::previousIndex(nextPosition, static_cast<int>(visibleIndices.size()));
      if (visibleIndices[nextPosition] >= 0) break;
    }
    if (visibleIndices[nextPosition] < 0) return;
    selectorIndex = static_cast<size_t>(visibleIndices[nextPosition]);
    const int pageItems = getPageItems(libraryRetainedContentHeight);
    if (pageItems <= 0 || currentPosition / pageItems != nextPosition / pageItems) {
      librarySelectionOnlyRender = false;
      libraryHasRetainedGridRects = false;
      libraryCoverManager.cancelPrefetch();
    }
    clampSelector();
    requestUpdate();
    return;
  }
  const int listSize = static_cast<int>(files.size());
  const size_t oldIndex = selectorIndex;
  const int next = delta > 0 ? ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)
                             : ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
  selectorIndex = static_cast<size_t>(next);
  const int pageItems = getPageItems(libraryRetainedContentHeight);
  if (pageItems <= 0 || static_cast<int>(oldIndex) / pageItems != next / pageItems) {
    librarySelectionOnlyRender = false;
    libraryHasRetainedGridRects = false;
    libraryCoverManager.cancelPrefetch();
  }
  clampSelector();
  requestUpdate();
}

void LibraryActivity::moveBookshelfVertical(const int delta) {
  if (files.empty()) return;
  lastNavigationInputMs = millis();
  libraryWorkPaused = true;
  libraryDirtyCoverIndex = -1;
  libraryPreviousSelectorIndex = selectorIndex;
  librarySelectionOnlyRender = true;
  const int columns = getBookshelfColumns();
  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) return;
    const int listSize = static_cast<int>(visibleIndices.size());
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    int next = currentPosition + delta * columns;
    if (next < 0) {
      const int column = currentPosition % columns;
      const int lastRowStart = ((listSize - 1) / columns) * columns;
      next = std::min(lastRowStart + column, listSize - 1);
    } else if (next >= listSize) {
      next = currentPosition % columns;
      if (next >= listSize) next = listSize - 1;
    }
    const int step = delta > 0 ? 1 : -1;
    while (next >= 0 && next < listSize && visibleIndices[next] < 0) {
      next += step;
    }
    if (next < 0 || next >= listSize || visibleIndices[next] < 0) return;
    selectorIndex = static_cast<size_t>(visibleIndices[next]);
    const int pageItems = getPageItems(libraryRetainedContentHeight);
    if (pageItems <= 0 || currentPosition / pageItems != next / pageItems) {
      librarySelectionOnlyRender = false;
      libraryHasRetainedGridRects = false;
      libraryCoverManager.cancelPrefetch();
    }
    clampSelector();
    requestUpdate();
    return;
  }
  const int listSize = static_cast<int>(files.size());
  const size_t oldIndex = selectorIndex;
  int next = static_cast<int>(selectorIndex) + delta * columns;
  if (next < 0) {
    const int column = static_cast<int>(selectorIndex) % columns;
    const int lastRowStart = ((listSize - 1) / columns) * columns;
    next = std::min(lastRowStart + column, listSize - 1);
  } else if (next >= listSize) {
    next = static_cast<int>(selectorIndex) % columns;
    if (next >= listSize) next = listSize - 1;
  }
  selectorIndex = static_cast<size_t>(next);
  const int pageItems = getPageItems(libraryRetainedContentHeight);
  if (pageItems <= 0 || static_cast<int>(oldIndex) / pageItems != next / pageItems) {
    librarySelectionOnlyRender = false;
    libraryHasRetainedGridRects = false;
    libraryCoverManager.cancelPrefetch();
  }
  clampSelector();
  requestUpdate();
}

void LibraryActivity::moveBookshelfPage(const int delta, const int pageItems) {
  if (files.empty() || pageItems <= 0) return;
  lastNavigationInputMs = millis();
  libraryWorkPaused = true;
  libraryDirtyCoverIndex = -1;
  libraryPreviousSelectorIndex = selectorIndex;
  librarySelectionOnlyRender = false;
  libraryHasRetainedGridRects = false;
  libraryCoverManager.cancelPrefetch();
  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    if (visibleIndices.empty()) return;
    const int currentPosition = getVisibleDashboardPosition(visibleIndices);
    const int totalPages = (static_cast<int>(visibleIndices.size()) + pageItems - 1) / pageItems;
    if (totalPages <= 1) return;
    const int currentPage = currentPosition / pageItems;
    const int nextPage = (currentPage + delta + totalPages) % totalPages;
    const int nextPageStart = nextPage * pageItems;
    const int nextPageEnd = std::min(static_cast<int>(visibleIndices.size()), nextPageStart + pageItems);
    for (int position = nextPageStart; position < nextPageEnd; ++position) {
      if (visibleIndices[position] >= 0) {
        selectorIndex = static_cast<size_t>(visibleIndices[position]);
        break;
      }
    }
  } else {
    const int listSize = static_cast<int>(files.size());
    selectorIndex = delta > 0 ? ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems)
                              : ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
  }
  clampSelector();
  requestUpdate();
}

void LibraryActivity::renderBookshelf(const Rect& rect, const int pageItems) {
  if (isLibraryDashboard()) {
    renderLibraryDashboard(rect, pageItems);
    return;
  }
  libraryRetainedItemRects.assign(files.size(), Rect{});
  libraryHasRetainedGridRects = true;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int columns = getBookshelfColumns();
  const int cardGap = BOOKSHELF_CARD_GAP;
  const int cardWidth = (rect.width - sidePadding * 2 - cardGap * (columns - 1)) / columns;
  const int pageStartIndex = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + pageItems);
  const int cardHeight = std::max(SHELF_COVER_MIN_HEIGHT + COVER_GRID_PAD * 2,
                                  (rect.height - cardGap * (getBookshelfRows() - 1)) / getBookshelfRows());

  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    const int localIndex = index - pageStartIndex;
    const int column = localIndex % columns;
    const int row = localIndex / columns;
    const int cardX = rect.x + sidePadding + column * (cardWidth + cardGap);
    const int cardY = rect.y + row * (cardHeight + cardGap);
    const Rect card{cardX, cardY, cardWidth, cardHeight};
    const bool selected = selectorIndex == static_cast<size_t>(index);

    const std::string& entry = files[index];
    const bool isFolder = !entry.empty() && entry.back() == '/';
    const uint8_t* icon = fileIconBitmap(entry);
    const int cellPad = isFolder ? CARD_PAD : COVER_GRID_PAD;
    const Rect inner = insetRect(card, cellPad);
    const int textX = inner.x;
    const int textWidth = inner.width;
    const int statusStripHeight = 26;
    const int statusStripTop = card.y + card.height - CARD_PAD - statusStripHeight;

    std::string meta;
    if (isFolder) {
      drawContainedCard(renderer, card, selected, 6);
      const int visualX = inner.x;
      const int visualY = inner.y;
      renderer.drawIcon(icon, visualX, visualY, BOOKSHELF_FOLDER_ICON_SIZE, BOOKSHELF_FOLDER_ICON_SIZE);
      const uint16_t folderItems =
          (index >= 0 && index < static_cast<int>(folderItemCounts.size())) ? folderItemCounts[index] : 0;
      meta = std::to_string(folderItems) + " " + I18N.get(folderItems == 1 ? StrId::STR_ITEM : StrId::STR_ITEMS);

      const int titleTop = visualY + BOOKSHELF_FOLDER_ICON_SIZE + 10;
      const int titleBottom = statusStripTop - 6;
      const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, getEntryTitle(index).c_str(), textWidth, 4);
      int lineY = titleTop;
      for (const auto& line : titleLines) {
        if (lineY > titleBottom - renderer.getLineHeight(UI_10_FONT_ID)) break;
        renderer.drawText(UI_10_FONT_ID, textX, lineY, line.c_str(), true);
        lineY += renderer.getLineHeight(UI_10_FONT_ID);
      }
      renderer.drawLine(inner.x, statusStripTop, inner.x + inner.width - 1, statusStripTop, true);
      const std::string statusText = renderer.truncatedText(SMALL_FONT_ID, meta.c_str(), inner.width - 8);
      const int statusW = renderer.getTextWidth(SMALL_FONT_ID, statusText.c_str());
      renderer.drawText(SMALL_FONT_ID, inner.x + std::max(0, (inner.width - statusW) / 2), statusStripTop + 6,
                        statusText.c_str(), true);
    } else {
      const bool coverShelf = isLibraryShelf() || isLibraryAuthorView() || isLibrarySeriesView();
      const bool bookTile = coverShelf || basepath != "/";
      if (index >= 0 && index < static_cast<int>(libraryRetainedItemRects.size())) {
        libraryRetainedItemRects[index] = card;
      }
      if (bookTile) {
        renderLibraryCard(card, index, selected, true);
      } else {
        const Rect visualRect{card.x + (card.width - std::min(inner.width, 58)) / 2, inner.y,
                              std::min(inner.width, 58), 52};
        drawBookPlaceholder(renderer, visualRect, icon == Image24Icon);
        renderSelectionMarker(card, selected);
      }
    }
  }
}

void LibraryActivity::renderAuthorGroupCard(const Rect& card, const int index, const bool selected) {
  drawContainedCard(renderer, card, selected, 6);
  const Rect inner = insetRect(card, COVER_GRID_PAD + 8);
  const int iconW = std::min(54, std::max(38, inner.width / 2));
  const int iconH = std::max(28, iconW * 72 / 100);
  const int iconY = inner.y + std::max(8, (inner.height - iconH) / 4);
  drawFolderGlyph(renderer, Rect{inner.x + (inner.width - iconW) / 2, iconY, iconW, iconH});

  const std::string title = renderer.truncatedText(UI_10_FONT_ID, getEntryTitle(index).c_str(), inner.width,
                                                   EpdFontFamily::BOLD);
  const std::string count = renderer.truncatedText(SMALL_FONT_ID, getEntrySubtitle(index).c_str(), inner.width);
  const int titleW = renderer.getTextWidth(UI_10_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  int textY = iconY + iconH + 14;
  renderer.drawText(UI_10_FONT_ID, inner.x + std::max(0, (inner.width - titleW) / 2), textY, title.c_str(), true,
                    EpdFontFamily::BOLD);
  if (!count.empty()) {
    const int countW = renderer.getTextWidth(SMALL_FONT_ID, count.c_str());
    textY += renderer.getLineHeight(UI_10_FONT_ID) + 4;
    renderer.drawText(SMALL_FONT_ID, inner.x + std::max(0, (inner.width - countW) / 2), textY, count.c_str(), true);
  }
  renderSelectionMarker(card, selected);
}

void LibraryActivity::renderLibraryDashboard(const Rect& rect, const int pageItems) {
  if (pageItems <= 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int bookColumns = getBookshelfColumns();
  const int toReadColumns = 3;
  const int cardGap = BOOKSHELF_CARD_GAP;
  const int bookCardWidth = (rect.width - sidePadding * 2 - cardGap * (bookColumns - 1)) / bookColumns;
  const auto visibleIndices = getVisibleDashboardIndices();
  if (visibleIndices.empty()) return;
  libraryRetainedItemRects.assign(files.size(), Rect{});
  libraryHasRetainedGridRects = true;
  const int visiblePosition = getVisibleDashboardPosition(visibleIndices);
  const int pageStartPosition = (visiblePosition / pageItems) * pageItems;
  const int pageEndPosition = std::min(static_cast<int>(visibleIndices.size()), pageStartPosition + pageItems);
  const bool showToReadHeading =
      libraryFilterMode == LIBRARY_FILTER_ALL && pageStartPosition == 0 && !libraryFileStates.empty() &&
      visibleIndices[0] < static_cast<int>(libraryFileStates.size()) &&
      libraryFileStates[visibleIndices[0]] == LIBRARY_STATE_TO_READ;
  int firstNormalPosition = -1;
  for (int position = pageStartPosition; position < pageEndPosition; ++position) {
    const int index = visibleIndices[position];
    if (index < 0) continue;
    if (index < static_cast<int>(entryPaths.size()) && !entryPaths[index].empty() &&
        libraryFileStates[index] != LIBRARY_STATE_TO_READ && libraryFileStates[index] != LIBRARY_STATE_FINISHED) {
      firstNormalPosition = position;
      break;
    }
  }
  const bool showAllBooksAfterShelf = false;
  const int headingHeight = showToReadHeading ? LIBRARY_SECTION_LABEL_HEIGHT : 0;
  const int allBooksLabelHeight = 0;
  const int toReadSectionExtra = showToReadHeading ? LIBRARY_TO_READ_SECTION_PAD * 2 : 0;
  const int gridTop = rect.y + headingHeight + (showToReadHeading ? LIBRARY_TO_READ_LABEL_GAP : 0);
  int toReadVisible = 0;
  if (showToReadHeading) {
    for (int position = pageStartPosition; position < pageEndPosition; ++position) {
      const int index = visibleIndices[position];
      if (index < 0) continue;
      if (toReadVisible < toReadColumns && index < static_cast<int>(libraryFileStates.size()) &&
          libraryFileStates[index] == LIBRARY_STATE_TO_READ) {
        ++toReadVisible;
      }
    }
  }
  const int toReadRows = toReadVisible > 0 ? 1 : 0;
  const int gridHeight = std::max(SHELF_COVER_MIN_HEIGHT + COVER_GRID_PAD * 2,
                                  rect.height - headingHeight - allBooksLabelHeight - toReadSectionExtra);
  const int bookCardHeight = std::max(SHELF_COVER_MIN_HEIGHT + COVER_GRID_PAD * 2,
                                      (gridHeight - cardGap * (getBookshelfRows() - 1)) / getBookshelfRows());

  if (showToReadHeading) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, rect.y, tr(STR_TO_READ), true, EpdFontFamily::BOLD);
    if (toReadVisible > 0) {
      const Rect toReadCard{sidePadding, gridTop - LIBRARY_TO_READ_SECTION_PAD,
                            rect.width - sidePadding * 2,
                            toReadRows * bookCardHeight + std::max(0, toReadRows - 1) * cardGap +
                                LIBRARY_TO_READ_SECTION_PAD * 2};
      renderer.drawRoundedRect(toReadCard.x, toReadCard.y, toReadCard.width, toReadCard.height, 2, 6, true);
    }
  }

  for (int position = pageStartPosition; position < pageEndPosition; ++position) {
    const int index = visibleIndices[position];
    int localIndex = position - pageStartPosition;
    if (index < 0) {
      continue;
    }
    const bool isToReadPreview =
        showToReadHeading && index < static_cast<int>(libraryFileStates.size()) &&
        libraryFileStates[index] == LIBRARY_STATE_TO_READ;
    const int toReadLocalIndex = position - pageStartPosition;
    if (isToReadPreview && toReadLocalIndex >= toReadVisible) {
      continue;
    }
    const bool selected = selectorIndex == static_cast<size_t>(index);
    const bool isShelf = index >= 0 && index < static_cast<int>(libraryFileStates.size()) &&
                         index < static_cast<int>(entryPaths.size()) && entryPaths[index].empty() &&
                         (libraryFileStates[index] == LIBRARY_VIEW_TO_READ ||
                          libraryFileStates[index] == LIBRARY_VIEW_FINISHED);

    const int toReadCardWidth = (rect.width - sidePadding * 2 - cardGap * (toReadColumns - 1)) / toReadColumns;
    const int normalLocalIndex = firstNormalPosition >= 0 ? position - firstNormalPosition : localIndex;
    const int column = isToReadPreview ? toReadLocalIndex % toReadColumns : localIndex % bookColumns;
    const int row = isToReadPreview ? toReadLocalIndex / toReadColumns : localIndex / bookColumns;
    const int cardWidth = isToReadPreview ? toReadCardWidth : bookCardWidth;
    const int cardX = rect.x + sidePadding + column * (cardWidth + cardGap);
    const int normalGridTop = showToReadHeading ? gridTop + bookCardHeight + cardGap + allBooksLabelHeight : gridTop;
    const int cardY = isToReadPreview ? gridTop + row * (bookCardHeight + cardGap)
                                      : normalGridTop + std::max(0, normalLocalIndex / bookColumns) *
                                                            (bookCardHeight + cardGap);
    const Rect card{cardX, cardY, cardWidth, bookCardHeight};
    if (index >= 0 && index < static_cast<int>(libraryRetainedItemRects.size())) {
      libraryRetainedItemRects[index] = card;
    }
    const Rect inner = insetRect(card, isShelf ? CARD_PAD : COVER_GRID_PAD);
    const bool isAuthorGroup = index >= 0 && index < static_cast<int>(entryTypes.size()) &&
                               entryTypes[index] == ENTRY_TYPE_AUTHOR_GROUP;
    if (isAuthorGroup) {
      renderAuthorGroupCard(card, index, selected);
      continue;
    }

    if (isShelf) {
      drawContainedCard(renderer, card, selected, 5);

      const int iconSize = 28;
      renderer.drawIcon(BookIcon, inner.x + (inner.width - iconSize) / 2, inner.y + 8, iconSize, iconSize);
      const int textWidth = inner.width;
      const auto titleLines =
          renderer.wrappedText(UI_10_FONT_ID, getEntryTitle(index).c_str(), textWidth, 2, EpdFontFamily::BOLD);
      int y = inner.y + iconSize + 18;
      for (const auto& line : titleLines) {
        const int lineW = renderer.getTextWidth(UI_10_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
        renderer.drawText(UI_10_FONT_ID, inner.x + std::max(0, (inner.width - lineW) / 2), y, line.c_str(), true,
                          EpdFontFamily::BOLD);
        y += renderer.getLineHeight(UI_10_FONT_ID);
      }
      const auto subtitleLines = renderer.wrappedText(SMALL_FONT_ID, getEntrySubtitle(index).c_str(), inner.width, 2);
      y += 4;
      for (const auto& line : subtitleLines) {
        if (y > inner.y + inner.height - renderer.getLineHeight(SMALL_FONT_ID)) break;
        const int lineW = renderer.getTextWidth(SMALL_FONT_ID, line.c_str());
        renderer.drawText(SMALL_FONT_ID, inner.x + std::max(0, (inner.width - lineW) / 2), y, line.c_str(), true);
        y += renderer.getLineHeight(SMALL_FONT_ID);
      }
      continue;
    }

    if (index >= 0 && index < static_cast<int>(libraryRetainedItemRects.size())) {
      libraryRetainedItemRects[index] = card;
    }
    renderLibraryCard(card, index, selected, true);
  }
}

void LibraryActivity::renderSelectionMarker(const Rect& rect, const bool selected) const {
  if (rect.width <= 0 || rect.height <= 0) return;
  if (!selected) {
    return;
  }
  const Rect focus = insetRect(rect, -3);
  renderer.drawRoundedRect(focus.x, focus.y, focus.width, focus.height, 3,
                           RecentBooksGrid::kCoverCornerRadius + 3, true);
}

void LibraryActivity::renderLibraryCard(const Rect& card, const int index, const bool selected,
                                        const bool allowDiskCover) {
  if (index < 0 || index >= static_cast<int>(entryPaths.size()) || card.width <= 0 || card.height <= 0) return;
  renderer.fillRect(card.x, card.y, card.width, card.height, false);

  const bool isAuthorGroup =
      index < static_cast<int>(entryTypes.size()) && entryTypes[index] == ENTRY_TYPE_AUTHOR_GROUP;
  if (isAuthorGroup) {
    renderAuthorGroupCard(card, index, selected);
    return;
  }

  if (selected) {
    const Rect focus = insetRect(card, -3);
    renderer.fillRoundedRect(focus.x, focus.y, focus.width, focus.height, RecentBooksGrid::kCoverCornerRadius + 3,
                             Color::LightGray);
  }
  const Rect inner = insetRect(card, COVER_GRID_PAD);
  const Rect coverArea = libraryCardCoverArea(renderer, inner);
  const Rect coverRect = calculateBookCoverRect(card, coverArea, coverArea.y + coverArea.height, 0, 0);
  bool coverDrawn = false;
  const bool epubBook = index < static_cast<int>(entryPaths.size()) && FsHelpers::hasEpubExtension(entryPaths[index]);
  if (epubBook && index < static_cast<int>(entryCoverPaths.size())) {
    const std::string& bookPath =
        index < static_cast<int>(resolvedRows.size()) && !resolvedRows[index].path.empty() ? resolvedRows[index].path
                                                                                           : entryPaths[index];
    const std::string& coverPath =
        index < static_cast<int>(resolvedRows.size()) ? resolvedRows[index].coverPath : entryCoverPaths[index];
    coverDrawn = libraryCoverManager.drawRendered(renderer, bookPath, coverPath, coverRect.x, coverRect.y,
                                                  coverRect.width, coverRect.height);
    (void)allowDiskCover;
  }
  if (!coverDrawn) {
    drawBookPlaceholder(renderer, coverRect, false);
    if (!epubBook && index < static_cast<int>(entryPaths.size())) {
      const std::string extension = renderer.truncatedText(SMALL_FONT_ID, getFileExtension(entryPaths[index]).c_str(),
                                                           coverRect.width - 10, EpdFontFamily::BOLD);
      if (!extension.empty()) {
        const int extW = renderer.getTextWidth(SMALL_FONT_ID, extension.c_str(), EpdFontFamily::BOLD);
        const int extY = coverRect.y + coverRect.height - renderer.getLineHeight(SMALL_FONT_ID) - 8;
        renderer.drawText(SMALL_FONT_ID, coverRect.x + std::max(0, (coverRect.width - extW) / 2), extY,
                          extension.c_str(), true, EpdFontFamily::BOLD);
      }
    }
  }
  const uint8_t progress =
      index >= 0 && index < static_cast<int>(resolvedRows.size())
          ? resolvedRows[index].progressPercent
          : (index >= 0 && index < static_cast<int>(progressFileStates.size()) ? progressFileStates[index] : 0);
  drawCenteredLibraryCardText(renderer, inner, coverArea, getEntryTitle(index), getEntrySubtitle(index), progress);
  const bool finished = (index >= 0 && index < static_cast<int>(resolvedRows.size()) && resolvedRows[index].finished) ||
                        (index >= 0 && index < static_cast<int>(libraryFileStates.size()) &&
                         libraryFileStates[index] == LIBRARY_STATE_FINISHED);
  if (finished) {
    constexpr int badgeSize = 16;
    const int badgeX = std::min(card.x + card.width - badgeSize - 6, coverRect.x + coverRect.width - badgeSize / 2);
    const int badgeY = coverRect.y + 4;
    renderer.fillRoundedRect(badgeX, badgeY, badgeSize, badgeSize, 4, Color::Black);
    renderer.drawLine(badgeX + 4, badgeY + 8, badgeX + 7, badgeY + 11, 2, false);
    renderer.drawLine(badgeX + 7, badgeY + 11, badgeX + 13, badgeY + 5, 2, false);
  }
  renderSelectionMarker(card, selected);
}

void LibraryActivity::renderMetadataStrip(const int pageWidth, const int pageHeight, const int contentHeight,
                                              const int pathLineHeight) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  const int separatorY = pathY - metrics.verticalSpacing / 2;
  const int stripHeight = std::max(pathLineHeight + metrics.verticalSpacing + 8, pageHeight - separatorY);
  renderer.fillRect(0, separatorY, pageWidth, stripHeight, false);
  renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);

  std::string pageLabel;
  int pageLabelWidth = 0;
  constexpr int metadataPageGap = 12;
  const int fullStripWidth = pageWidth - metrics.contentSidePadding * 2;
  int pathMaxWidth = fullStripWidth;
  std::string infoText = librarySafeMode ? tr(STR_LIBRARY_SAFE_MODE_BODY) : basepath;
  if (usesBookshelfGrid() && !files.empty() && selectorIndex < files.size()) {
    const bool selectedAuthorGroup =
        selectorIndex < entryTypes.size() && entryTypes[selectorIndex] == ENTRY_TYPE_AUTHOR_GROUP;
    const bool selectedBook =
        selectorIndex < entryPaths.size() && !entryPaths[selectorIndex].empty() &&
        (files[selectorIndex].empty() || files[selectorIndex].back() != '/') &&
        (!isLibraryDashboard() || isLibraryAuthorView() || selectorIndex >= LIBRARY_DASHBOARD_SHORTCUT_COUNT);
    if (selectedAuthorGroup) {
      infoText = getEntryTitle(static_cast<int>(selectorIndex));
      const std::string count = getEntrySubtitle(static_cast<int>(selectorIndex));
      if (!count.empty()) infoText += " - " + count;
      infoText += " - ";
      infoText += tr(STR_OPEN_AUTHOR);
    } else if (selectedBook) {
      infoText = getEntryTitle(static_cast<int>(selectorIndex));
      const std::string author = getEntrySubtitle(static_cast<int>(selectorIndex));
      if (!author.empty()) infoText += " - " + author;
      const std::string progress = getLibraryStateLabel(static_cast<int>(selectorIndex));
      if (!progress.empty() && progress.find('%') != std::string::npos) infoText += " - " + progress;
    }
    int visibleCount = static_cast<int>(files.size());
    int visiblePosition = static_cast<int>(selectorIndex);
    const int stripPageItems = getPageItems(contentHeight);
    if (isLibraryDashboard()) {
      const auto visibleIndices = getVisibleDashboardIndices();
      visibleCount = static_cast<int>(visibleIndices.size());
      visiblePosition = getVisibleDashboardPosition(visibleIndices);
    }
    if (stripPageItems > 0 && visibleCount > stripPageItems) {
      const int pageCount = (visibleCount + stripPageItems - 1) / stripPageItems;
      const int currentPage = std::min(pageCount, visiblePosition / stripPageItems + 1);
      pageLabel = std::to_string(currentPage) + "/" + std::to_string(pageCount);
      pageLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, pageLabel.c_str(), EpdFontFamily::BOLD);
      pathMaxWidth = std::max(32, fullStripWidth - pageLabelWidth - metadataPageGap);
    }
  }

  const char* pathStr = infoText.c_str();
  const char* pathDisplay = pathStr;
  char leftTruncBuf[256];
  if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
    const char ellipsis[] = "\xe2\x80\xa6";
    const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
    const int available = pathMaxWidth - ellipsisWidth;
    const char* p = pathStr;
    while (*p) {
      if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
      ++p;
      while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
    }
    snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
    pathDisplay = leftTruncBuf;
  }
  const int textW = renderer.getTextWidth(SMALL_FONT_ID, pathDisplay);
  const int centeredTextX = metrics.contentSidePadding + std::max(0, (fullStripWidth - textW) / 2);
  const int maxTextX = metrics.contentSidePadding + std::max(0, pathMaxWidth - textW);
  const int textX = std::clamp(centeredTextX, metrics.contentSidePadding, maxTextX);
  renderer.drawText(SMALL_FONT_ID, textX, pathY, pathDisplay);
  if (!pageLabel.empty()) {
    const int pageX = pageWidth - metrics.contentSidePadding - pageLabelWidth;
    renderer.drawText(SMALL_FONT_ID, pageX, pathY, pageLabel.c_str(), true, EpdFontFamily::BOLD);
  }

  libraryRetainedMetadataRect = Rect{0, separatorY, pageWidth, stripHeight};
  libraryRetainedContentHeight = contentHeight;
}

void LibraryActivity::renderLibraryProgressHub(const int pageWidth, const int pageHeight, const int pageItems) {
  if (!usesBookshelfGrid()) return;

  int current = 0;
  int total = 0;
  bool showProgressBar = true;
  bool showActivityIndicator = false;
  std::string title = tr(STR_REFRESH_LIBRARY);
  std::string detail;
  std::array<std::string, 6> summaryLines;
  size_t summaryLineCount = 0;
  auto addSummaryLine = [&](std::string line) {
    if (summaryLineCount < summaryLines.size()) {
      summaryLines[summaryLineCount++] = std::move(line);
    }
  };
  const int pendingCovers = countPendingCoverJobs(pageItems);
  const bool passiveIndexing = libraryProgressAction == LIBRARY_PROGRESS_NONE && libraryIndexingActive;
  if (libraryProgressAction == LIBRARY_PROGRESS_NONE && !passiveIndexing && !libraryIndexSummaryVisible) return;

  if (libraryIndexSummaryVisible) {
    title = libraryIndexCanceled ? tr(STR_INDEXING_CANCELED) : tr(STR_LIBRARY_INDEXED_SUCCESSFULLY);
    showProgressBar = false;
    if (libraryIndexCanceled) {
      detail = std::string(tr(STR_BOOKS_INDEXED)) + ": " + std::to_string(files.size());
    } else {
      detail = std::string(tr(STR_BOOKS)) + ": " + std::to_string(files.size()) + " | " +
               tr(STR_METADATA_FAILURES) + ": " + std::to_string(librarySkippedBookCount);
      if (librarySkippedBookCount > 0) {
        addSummaryLine(tr(STR_SKIPPED_METADATA));
      }
      for (const auto& skipped : librarySkippedBooks) {
        if (summaryLineCount >= 5) break;
        const std::string label = skipped.title.empty() ? skipped.filename : skipped.title;
        addSummaryLine(label);
      }
      const size_t visibleSkipped = summaryLineCount == 0 ? 0 : summaryLineCount - 1;
      if (librarySkippedBookCount > visibleSkipped) {
        addSummaryLine(std::to_string(librarySkippedBookCount - visibleSkipped) + " " + tr(STR_MORE));
      }
    }
  } else {

    if (libraryProgressAction == LIBRARY_PROGRESS_REBUILD) {
      title = tr(STR_RESET_LIBRARY_INDEX);
    } else if (libraryProgressAction == LIBRARY_PROGRESS_COVERS) {
      title = libraryPostIndexCoverWarmup ? tr(STR_PREPARING_COVERS) : tr(STR_REFRESH_COVERS);
    } else if (passiveIndexing) {
      title = libraryIndexStage == LIBRARY_INDEX_STAGE_METADATA
                  ? tr(STR_READING_BOOK_METADATA)
                  : std::string(tr(STR_INDEXING)) + " " + tr(STR_LIBRARY);
    } else if (libraryProgressAction == LIBRARY_PROGRESS_REFRESH) {
      title = tr(STR_REFRESH_LIBRARY);
    }

    if (libraryProgressAction == LIBRARY_PROGRESS_COVERS) {
      total = libraryCoverWarmupTotal > 0 ? static_cast<int>(libraryCoverWarmupTotal)
                                          : countCoverWindowCandidates(pageItems);
      current = std::min(static_cast<int>(libraryCoverWarmupDone), total);
      if (total <= 0) {
        return;
      }
      detail = std::string(tr(STR_COVER)) + " " + std::to_string(std::min(current + 1, total)) + " / " +
               std::to_string(total);
      if (!libraryCurrentCoverTitle.empty()) {
        addSummaryLine(libraryCurrentCoverTitle);
      }
    } else {
      if (!libraryIndexingActive) {
        if (libraryProgressAction == LIBRARY_PROGRESS_REFRESH || libraryProgressAction == LIBRARY_PROGRESS_REBUILD) {
          libraryProgressAction = LIBRARY_PROGRESS_NONE;
        }
        return;
      }
      if (libraryIndexStage == LIBRARY_INDEX_STAGE_METADATA) {
        showProgressBar = true;
        total = static_cast<int>(libraryDiscoveredBookPaths.size());
        current = std::min(static_cast<int>(libraryMetadataResolveIndex), total);
        const int displayCurrent = total > 0 ? std::min(current + 1, total) : 0;
        detail = std::string(tr(STR_BOOK)) + " " + std::to_string(displayCurrent) + " / " +
                 std::to_string(std::max(current, total));
        const std::string bookLine = libraryCurrentMetadataTitle.empty() ? titleFromPath(libraryCurrentMetadataPath)
                                                                         : libraryCurrentMetadataTitle;
        const std::string authorLine =
            libraryCurrentMetadataAuthor.empty() ? authorDisplayName("", libraryCurrentMetadataPath)
                                                 : libraryCurrentMetadataAuthor;
        if (!bookLine.empty()) {
          addSummaryLine(bookLine);
        }
        if (!authorLine.empty() && authorLine != tr(STR_UNKNOWN_AUTHOR)) {
          addSummaryLine(authorLine);
        }
      } else if (libraryIndexStage == LIBRARY_INDEX_STAGE_FINALIZE) {
        showProgressBar = true;
        total = 1;
        current = 1;
        title = tr(STR_FINALIZING_LIBRARY);
        detail.clear();
      } else {
        showProgressBar = false;
        showActivityIndicator = true;
        total = 0;
        current = static_cast<int>(libraryProgressBookCount);
        detail = std::string(tr(STR_BOOKS_FOUND)) + ": " + std::to_string(current);
        addSummaryLine(std::string(tr(STR_FOLDERS_SCANNED)) + ": " +
                       std::to_string(libraryProgressFoldersScanned));
        std::string searching = tr(STR_SEARCHING);
        while (!searching.empty() && searching.back() == '.') {
          searching.pop_back();
        }
        const uint8_t dots = static_cast<uint8_t>((millis() / 350) % 4);
        searching.append(dots, '.');
        addSummaryLine(searching);
      }
    }
  }

  const int hubW = std::min(330, pageWidth - 44);
  const int hubH =
      summaryLineCount == 0 ? 104 : std::min(pageHeight - 74, 132 + static_cast<int>(summaryLineCount) * 16);
  const int hubX = (pageWidth - hubW) / 2;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentHeight = std::max(hubH, contentBottom - contentTop);
  const int hubY = std::clamp(contentTop + (contentHeight - hubH) / 2, 0, std::max(0, pageHeight - hubH));
  renderer.fillRect(std::max(0, hubX - 10), std::max(0, hubY - 10),
                    std::min(pageWidth - std::max(0, hubX - 10), hubW + 20),
                    std::min(pageHeight - std::max(0, hubY - 10), hubH + 20), false);
  renderer.fillRoundedRect(hubX, hubY, hubW, hubH, 8, Color::White);
  renderer.drawRoundedRect(hubX, hubY, hubW, hubH, 2, 8, true);

  const int titleW = renderer.getTextWidth(UI_10_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, hubX + std::max(0, (hubW - titleW) / 2), hubY + 14, title.c_str(), true,
                    EpdFontFamily::BOLD);
  const std::string safeDetail = renderer.truncatedText(SMALL_FONT_ID, detail.c_str(), hubW - 34);
  const int countW = renderer.getTextWidth(SMALL_FONT_ID, safeDetail.c_str());
  renderer.drawText(SMALL_FONT_ID, hubX + std::max(0, (hubW - countW) / 2), hubY + 38, safeDetail.c_str(), true);

  const int barX = hubX + 30;
  const int barY = hubY + 58;
  const int barW = hubW - 60;
  const int fillW = total > 0 ? std::min(barW, std::max(0, current * barW / std::max(current, total))) : 0;
  if (showProgressBar || showActivityIndicator || summaryLineCount == 0) {
    renderer.drawRect(barX, barY, barW, 9, true);
    if (showProgressBar && fillW > 0) {
      renderer.fillRect(barX + 1, barY + 1, std::max(0, fillW - 2), 7, true);
    } else if (!showProgressBar) {
      const int tickW = std::max(20, barW / 5);
      const int tickX = barX + 1 + ((millis() / 220) % std::max(1, barW - tickW - 2));
      renderer.fillRect(tickX, barY + 1, tickW, 7, true);
    }
  }

  int nextY = summaryLineCount == 0 ? hubY + 82 : ((showProgressBar || showActivityIndicator) ? hubY + 78 : hubY + 62);
  if (summaryLineCount > 0) {
    for (size_t lineIndex = 0; lineIndex < summaryLineCount; ++lineIndex) {
      const auto& line = summaryLines[lineIndex];
      const std::string safeLine = renderer.truncatedText(SMALL_FONT_ID, line.c_str(), hubW - 34);
      const int lineW = renderer.getTextWidth(SMALL_FONT_ID, safeLine.c_str());
      renderer.drawText(SMALL_FONT_ID, hubX + std::max(0, (hubW - lineW) / 2), nextY, safeLine.c_str(), true);
      nextY += 16;
    }
  }
  const std::string cancel = libraryIndexSummaryVisible ? std::string(tr(STR_BACK)) : std::string(tr(STR_CANCEL_INDEXING));
  const int cancelW = renderer.getTextWidth(SMALL_FONT_ID, cancel.c_str());
  renderer.drawText(SMALL_FONT_ID, hubX + std::max(0, (hubW - cancelW) / 2),
                    std::min(hubY + hubH - 20, nextY + 2), cancel.c_str(), true);
}

bool LibraryActivity::renderLibrarySelectionOnly(const int pageWidth, const int pageHeight, const int contentHeight,
                                                 const int pathLineHeight) {
  const bool supportsSelectionOnly =
      isLibraryDashboard() || isLibraryShelf() || isLibraryAuthorView();
  if (!supportsSelectionOnly || contentHeight <= 0 || !libraryHasRetainedGridRects || libraryRetainedItemRects.empty() ||
      libraryPreviousSelectorIndex >= libraryRetainedItemRects.size() ||
      selectorIndex >= libraryRetainedItemRects.size() || libraryPreviousSelectorIndex == selectorIndex) {
    return false;
  }
  if (libraryRetainedRenderToken.empty() ||
      buildLibraryRetainedRenderToken(contentHeight) != libraryRetainedRenderToken) {
    libraryHasRetainedGridRects = false;
    libraryRetainedRenderToken.clear();
    return false;
  }
  const Rect oldRect = libraryRetainedItemRects[libraryPreviousSelectorIndex];
  const Rect newRect = libraryRetainedItemRects[selectorIndex];
  if (oldRect.width <= 0 || oldRect.height <= 0 || newRect.width <= 0 || newRect.height <= 0) {
    return false;
  }

  renderLibraryCard(oldRect, static_cast<int>(libraryPreviousSelectorIndex), false, false);
  renderLibraryCard(newRect, static_cast<int>(selectorIndex), true, false);
  if (!usesBookshelfGrid()) {
    renderMetadataStrip(pageWidth, pageHeight, contentHeight, pathLineHeight);
  } else {
    libraryRetainedMetadataRect = Rect{};
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

bool LibraryActivity::renderLibraryDirtyCoverCard(const int pageWidth, const int pageHeight) {
  if (millis() - lastNavigationInputMs < LIBRARY_BACKGROUND_IDLE_MS || mappedInput.isAnyMappedButtonPressed()) {
    libraryDirtyCoverIndex = -1;
    return false;
  }
  if (libraryDirtyCoverIndex < 0 || !usesBookshelfGrid() || !libraryHasRetainedGridRects ||
      libraryDirtyCoverIndex >= static_cast<int>(libraryRetainedItemRects.size()) ||
      libraryDirtyCoverIndex >= static_cast<int>(entryPaths.size())) {
    libraryDirtyCoverIndex = -1;
    return false;
  }
  const int index = libraryDirtyCoverIndex;
  const Rect card = libraryRetainedItemRects[index];
  if (card.width <= 0 || card.height <= 0) {
    libraryDirtyCoverIndex = -1;
    return false;
  }
  const int clearX = std::max(0, ((card.x - 10) / 8) * 8);
  const int clearY = std::max(0, card.y - 8);
  const int clearRight = std::min(pageWidth, ((card.x + card.width + 10 + 7) / 8) * 8);
  const int clearBottom = std::min(pageHeight, card.y + card.height + 8);
  renderer.fillRect(clearX, clearY, clearRight - clearX, clearBottom - clearY, false);

  const bool selected = selectorIndex == static_cast<size_t>(index);
  renderLibraryCard(card, index, selected, true);
  if (libraryProgressAction == LIBRARY_PROGRESS_COVERS) {
    renderLibraryProgressHub(pageWidth, pageHeight, usesBookshelfGrid() ? getPageItems(libraryRetainedContentHeight) : 0);
    libraryProgressHudOnlyRender = false;
  }

  libraryDirtyCoverIndex = -1;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

void LibraryActivity::render(RenderLock&&) {
  libraryRendering = true;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  if (libraryOverlayMode != LIBRARY_OVERLAY_NONE && libraryFirstRenderDone) {
    renderLibraryOverlay(pageWidth, pageHeight);
    lastLibraryRenderFinishedMs = millis();
    libraryRendering = false;
    return;
  }
  if (libraryProgressHudOnlyRender && libraryFirstRenderDone &&
      (libraryProgressAction != LIBRARY_PROGRESS_NONE || libraryIndexingActive)) {
    const int hubW = std::min(330, static_cast<int>(pageWidth) - 44);
    const int hubH = std::min(static_cast<int>(pageHeight) - 74, 228);
    const int hubX = (static_cast<int>(pageWidth) - hubW) / 2;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom = static_cast<int>(pageHeight) - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int contentHeight = std::max(hubH, contentBottom - contentTop);
    const int hubY =
        std::clamp(contentTop + (contentHeight - hubH) / 2, 0, std::max(0, static_cast<int>(pageHeight) - hubH));
    const int updateX = std::max(0, hubX - 12);
    const int updateY = std::max(0, hubY - 12);
    const int updateW = std::min(static_cast<int>(pageWidth) - updateX, hubW + 24);
    const int updateH = std::min(static_cast<int>(pageHeight) - updateY, hubH + 24);
    renderLibraryProgressHub(pageWidth, pageHeight, usesBookshelfGrid() ? getPageItems(libraryRetainedContentHeight) : 0);
    renderer.displayWindow(updateX, updateY, updateW, updateH);
    libraryProgressHudOnlyRender = false;
    lastLibraryRenderFinishedMs = millis();
    libraryRendering = false;
    return;
  }
  if (libraryDirtyCoverIndex >= 0 && renderLibraryDirtyCoverCard(pageWidth, pageHeight)) {
    lastLibraryRenderFinishedMs = millis();
    libraryRendering = false;
    return;
  }
  if (librarySelectionOnlyRender && usesBookshelfGrid() && !files.empty() &&
      renderLibrarySelectionOnly(pageWidth, pageHeight, libraryRetainedContentHeight, pathLineHeight)) {
    librarySelectionOnlyRender = false;
    lastLibraryRenderFinishedMs = millis();
    libraryRendering = false;
    return;
  }
  libraryDirtyCoverIndex = -1;
  renderer.clearScreen();

  std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  if (librarySafeMode) {
    folderName = tr(STR_LIBRARY_SAFE_MODE);
  } else if (usesBookshelfGrid() && basepath == "/") {
    if (isLibraryDashboard()) {
      folderName = tr(STR_LIBRARY);
    } else if (isLibraryAuthorView()) {
      folderName = libraryAuthorViewName.empty() ? tr(STR_AUTHOR) : libraryAuthorViewName;
    } else if (isLibrarySeriesView()) {
      folderName = librarySeriesViewName.empty() ? tr(STR_SERIES) : librarySeriesViewName;
    } else if (libraryView == LIBRARY_VIEW_TO_READ) {
      folderName = tr(STR_TO_READ);
    } else if (libraryView == LIBRARY_VIEW_FINISHED) {
      folderName = tr(STR_FINISHED_BOOKS);
    }
  }
  std::string headerStatusText;
  if (usesBookshelfGrid()) {
    int visibleBookCount = LibraryIndexModel::countRealBooks(entryPaths);
    if (isLibraryDashboard()) {
      visibleBookCount = static_cast<int>(getVisibleDashboardIndices().size());
    }
    if (!librarySearchQuery.empty()) {
      headerStatusText = std::string(tr(STR_SEARCH)) + ": " + librarySearchQuery + " | " +
                         std::to_string(visibleBookCount) + " " + tr(STR_BOOKS);
    } else {
      headerStatusText = std::string(tr(STR_SORT_BY)) + " " + librarySortLabel();
    }
    if (SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_TITLE ||
        SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_AUTHOR ||
        SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_RECENT ||
        SETTINGS.librarySort == CrossPointSettings::LIBRARY_SORT_PROGRESS) {
      headerStatusText += " (";
      headerStatusText += librarySortDirectionLabel();
      headerStatusText += ")";
    }
    if (librarySearchQuery.empty() && libraryFilterMode != LIBRARY_FILTER_ALL) {
      headerStatusText += " | ";
      headerStatusText += tr(STR_FILTER);
      headerStatusText += ": ";
      headerStatusText += libraryFilterLabel();
    }
    if (librarySearchQuery.empty()) {
      headerStatusText += " | ";
      headerStatusText += std::to_string(visibleBookCount);
      headerStatusText += " ";
      headerStatusText += tr(STR_BOOKS);
    }
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str(),
                 usesBookshelfGrid() && !headerStatusText.empty() ? headerStatusText.c_str() : nullptr);

  const int pathReserved = usesBookshelfGrid() ? 0 : pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const bool progressHudActive = usesBookshelfGrid() &&
                                 (libraryProgressAction != LIBRARY_PROGRESS_NONE || libraryIndexingActive);
  if (files.empty() && !progressHudActive) {
    std::string emptyTitle = usesBookshelfGrid() ? tr(STR_NO_BOOKS_HERE) : tr(STR_NO_FILES_FOUND);
    std::string emptySubtitle = tr(STR_ADD_EPUB_FILES);
    if (isLibraryDashboard() && libraryIndexingActive) {
      emptyTitle = tr(STR_INDEXING);
      emptySubtitle = tr(STR_LIBRARY);
    }
    if (isLibraryShelf()) {
      if (libraryView == LIBRARY_VIEW_FINISHED) {
        emptyTitle = tr(STR_NO_FINISHED_BOOKS);
      } else if (libraryView == LIBRARY_VIEW_TO_READ) {
        emptyTitle = tr(STR_NO_TO_READ_BOOKS);
      } else if (libraryView == LIBRARY_VIEW_CONTINUE) {
        emptyTitle = tr(STR_NO_BOOKS_IN_PROGRESS);
      }
      emptySubtitle = tr(STR_USE_BROWSE_FILES_TO_MARK);
    }
    if (usesBookshelfGrid()) {
      const Rect emptyCard{metrics.contentSidePadding, contentTop + 10,
                           pageWidth - metrics.contentSidePadding * 2, std::min(118, contentHeight - 20)};
      drawContainedCard(renderer, emptyCard, false, 7);
      const Rect inner = insetRect(emptyCard, 16);
      const auto titleLines =
          renderer.wrappedText(UI_10_FONT_ID, emptyTitle.c_str(), inner.width, 2, EpdFontFamily::BOLD);
      int y = inner.y + 4;
      for (const auto& line : titleLines) {
        renderer.drawText(UI_10_FONT_ID, inner.x, y, line.c_str(), true, EpdFontFamily::BOLD);
        y += renderer.getLineHeight(UI_10_FONT_ID);
      }
      const auto subtitleLines = renderer.wrappedText(SMALL_FONT_ID, emptySubtitle.c_str(), inner.width, 2);
      y += 8;
      for (const auto& line : subtitleLines) {
        if (y > inner.y + inner.height - renderer.getLineHeight(SMALL_FONT_ID)) break;
        renderer.drawText(SMALL_FONT_ID, inner.x, y, line.c_str(), true);
        y += renderer.getLineHeight(SMALL_FONT_ID);
      }
    } else {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20,
                        emptyTitle.c_str(), true, EpdFontFamily::BOLD);
    }
  } else if (usesBookshelfGrid()) {
    const Rect contentRect{0, contentTop, pageWidth, contentHeight};
    const int pageItems = getPageItems(contentHeight);
    updateActiveCoverWindow(pageItems);
    renderBookshelf(contentRect, pageItems);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return getFileName(files[index]); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index]); },
        [this](int index) { return getFileExtension(files[index]); }, false,
        [this](int index) {
          return index >= 0 && index < static_cast<int>(completedFileStates.size()) && completedFileStates[index] != 0;
        });
  }

  if (!usesBookshelfGrid()) {
    renderMetadataStrip(pageWidth, pageHeight, contentHeight, pathLineHeight);
  } else {
    libraryRetainedMetadataRect = Rect{};
    libraryRetainedContentHeight = contentHeight;
  }
  libraryRetainedRenderToken = usesBookshelfGrid() ? buildLibraryRetainedRenderToken(contentHeight) : std::string();

  if (usesBookshelfGrid()) {
    const int dividerY = metrics.topPadding + metrics.headerHeight - 3;
    renderer.drawLine(metrics.contentSidePadding, dividerY, pageWidth - metrics.contentSidePadding, dividerY, true);
  }

  if (usesBookshelfGrid() && !files.empty()) {
    const int pageItems = getPageItems(contentHeight);
    int visibleCount = static_cast<int>(files.size());
    int visiblePosition = static_cast<int>(selectorIndex);
    if (isLibraryDashboard()) {
      const auto visibleIndices = getVisibleDashboardIndices();
      visibleCount = static_cast<int>(visibleIndices.size());
      visiblePosition = getVisibleDashboardPosition(visibleIndices);
    }
    if (pageItems > 0 && visibleCount > pageItems && visiblePosition >= 0) {
      const int pageCount = (visibleCount + pageItems - 1) / pageItems;
      const int currentPage = std::min(pageCount, visiblePosition / pageItems + 1);
      const std::string pageLabel = std::to_string(currentPage) + "/" + std::to_string(pageCount);
      const int pageW = renderer.getTextWidth(SMALL_FONT_ID, pageLabel.c_str(), EpdFontFamily::BOLD);
      const int pageX = pageWidth - metrics.contentSidePadding - pageW;
      const int pageY = pageHeight - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID) - 2;
      renderer.drawText(SMALL_FONT_ID, pageX, pageY, pageLabel.c_str(), true, EpdFontFamily::BOLD);
    }
  }
  renderLibraryProgressHub(pageWidth, pageHeight, usesBookshelfGrid() ? getPageItems(contentHeight) : 0);

  const auto labels = usesBookshelfGrid()
                          ? mappedInput.mapLabels(tr(STR_BACK), files.empty() ? "" : tr(STR_SELECT),
                                                  files.empty() ? "" : tr(STR_DIR_LEFT),
                                                  files.empty() ? "" : tr(STR_DIR_RIGHT))
                          : mappedInput.mapLabels((basepath == "/" && !isLibraryShelf() &&
                                                    libraryView != LIBRARY_VIEW_FILES)
                                                       ? tr(STR_HOME)
                                                       : tr(STR_BACK),
                                                   files.empty() ? "" : tr(STR_OPEN),
                                                   files.empty() ? "" : tr(STR_DIR_UP),
                                                   files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (holdPreviewVisible) {
    drawHoldPreview(renderer, tr(STR_BOOK_ACTIONS));
  } else if (sortPreviewVisible) {
    drawHoldPreview(renderer, tr(STR_SORT_VIEW));
  }

  if (usesBookshelfGrid() && !libraryFirstRenderDone) {
    libraryHotFirstGridDrawMs = millis();
  }
  libraryFirstRenderDone = true;
  if (usesBookshelfGrid() && libraryRetainedRenderToken.size() > 0) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
  librarySelectionOnlyRender = false;
  lastLibraryRenderFinishedMs = millis();
  libraryRendering = false;
}

size_t LibraryActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
