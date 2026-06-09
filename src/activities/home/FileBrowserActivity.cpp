#include "FileBrowserActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>
#include <variant>

#include "../util/ConfirmationActivity.h"
#include "CrossPointSettings.h"
#include "LibraryMetadataStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/file.h"
#include "components/icons/folder.h"
#include "components/icons/image24.h"
#include "components/icons/text.h"
#include "fontIds.h"
#include "util/RecentBooksGrid.h"

std::string getFileExtension(std::string filename);
std::string getFileName(std::string filename);

namespace {
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
constexpr unsigned long LIBRARY_BACKGROUND_IDLE_MS = 650;
constexpr unsigned long LIBRARY_POST_RENDER_WORK_IDLE_MS = 900;
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
constexpr size_t MAX_LIBRARY_DASHBOARD_BOOKS = 240;
constexpr int MAX_LIBRARY_SCAN_DEPTH = 8;
constexpr int MAX_LIBRARY_FILES_PER_TICK = 12;
constexpr int MAX_LIBRARY_FOLDERS_PER_TICK = 1;
constexpr uint8_t MAX_LIBRARY_FAILURES = 8;
constexpr uint32_t MIN_LIBRARY_COVER_HEAP = 70000;
constexpr char LIBRARY_BREADCRUMB_FILE[] = "/.crosspoint/library_crash_breadcrumb.txt";
constexpr char LIBRARY_DASHBOARD_INDEX_FILE[] = "/.crosspoint/library_dashboard.tsv";
constexpr char LIBRARY_DASHBOARD_INDEX_TMP[] = "/.crosspoint/library_dashboard.tmp";
constexpr char LIBRARY_DASHBOARD_SIGNATURE_FILE[] = "/.crosspoint/library_dashboard.sig";
constexpr int LIBRARY_DASHBOARD_SHORTCUT_COUNT = 0;
constexpr uint8_t LIBRARY_STATE_SECTION = 250;
constexpr uint8_t LIBRARY_FILTER_ALL = 0;
constexpr uint8_t LIBRARY_FILTER_TO_READ = 1;
constexpr uint8_t LIBRARY_FILTER_FINISHED = 2;

enum BookAction : int {
  BOOK_ACTION_CONTINUE = 0,
  BOOK_ACTION_MARK_TO_READ = 1,
  BOOK_ACTION_MARK_FINISHED = 2,
  BOOK_ACTION_REMOVE_STATE = 3,
  BOOK_ACTION_DELETE = 4,
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
  SORT_VIEW_BROWSE_FILES = 10,
  SORT_VIEW_COLUMNS = 20,
  SORT_VIEW_REFRESH = 30,
};

std::string savedBrowserBasepath = "/";
uint8_t savedBrowserLibraryView = 0;
size_t savedBrowserSelectorIndex = 0;
bool hasSavedBrowserCursor = false;
uint8_t libraryFilterMode = LIBRARY_FILTER_ALL;
bool librarySortDescending = false;
bool libraryShowToReadShelf = true;

struct LibraryDashboardSnapshot {
  std::vector<std::string> files;
  std::vector<uint8_t> completedFileStates;
  std::vector<uint8_t> progressFileStates;
  std::vector<uint8_t> libraryFileStates;
  std::vector<uint16_t> folderItemCounts;
  std::vector<std::string> entryPaths;
  std::vector<std::string> entryTitles;
  std::vector<std::string> entrySubtitles;
  std::vector<std::string> entryCoverPaths;
  std::vector<std::string> entryCoverSourcePaths;
  std::vector<uint8_t> entryCoverStates;
  bool valid = false;
};

LibraryDashboardSnapshot& libraryDashboardSnapshot() {
  static LibraryDashboardSnapshot snapshot;
  return snapshot;
}

struct ThumbnailCacheRecord {
  std::string bookPath;
  std::string sourceCoverPath;
  std::string thumbPath;
  int width = 0;
  int height = 0;
  uint8_t state = ENTRY_COVER_UNKNOWN;
};

std::vector<ThumbnailCacheRecord>& thumbnailCache() {
  static std::vector<ThumbnailCacheRecord> cache;
  return cache;
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

void rememberThumbnailCacheRecord(const std::string& bookPath, const std::string& sourceCoverPath,
                                  const std::string& thumbPath, const int width, const int height,
                                  const uint8_t state) {
  auto& cache = thumbnailCache();
  if (auto* existing = findThumbnailCacheRecord(bookPath, sourceCoverPath, width, height)) {
    existing->thumbPath = thumbPath;
    existing->state = state;
    return;
  }
  constexpr size_t kMaxThumbnailCacheRecords = 96;
  if (cache.size() >= kMaxThumbnailCacheRecords) {
    cache.erase(cache.begin());
  }
  cache.push_back(ThumbnailCacheRecord{bookPath, sourceCoverPath, thumbPath, width, height, state});
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
  switch (SETTINGS.bookshelfColumns) {
    case CrossPointSettings::BOOKSHELF_LAYOUT_2X2:
      return tr(STR_LAYOUT_2X2);
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X3:
      return tr(STR_LAYOUT_3X3);
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X4:
      return tr(STR_LAYOUT_3X4);
    default:
      return tr(STR_LAYOUT_2X2);
  }
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
  return libraryShowToReadShelf ? tr(STR_ON) : tr(STR_OFF);
}

void cycleLibrarySortSetting() {
  switch (SETTINGS.librarySort) {
    case CrossPointSettings::LIBRARY_SORT_TITLE:
      SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_AUTHOR;
      break;
    case CrossPointSettings::LIBRARY_SORT_AUTHOR:
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
  switch (SETTINGS.bookshelfColumns) {
    case CrossPointSettings::BOOKSHELF_LAYOUT_2X2:
      return 170;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X3:
      return 108;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X4:
      return 86;
    default:
      return RecentBooksGrid::kCoverWidth;
  }
}

int libraryCoverTargetHeight() {
  switch (SETTINGS.bookshelfColumns) {
    case CrossPointSettings::BOOKSHELF_LAYOUT_2X2:
      return 271;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X3:
      return 172;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X4:
      return 137;
    default:
      return RecentBooksGrid::kCoverHeight;
  }
}

std::string lowercaseCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
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

std::string authorSortKey(const std::string& cachedAuthor, const std::string& path, const std::string& title) {
  std::string author = trimCopy(cachedAuthor);
  if (author.empty()) {
    author = trimCopy(parentFolderName(path));
  }
  if (author.empty()) {
    author = trimCopy(title);
  }
  return lowercaseCopy(author);
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

std::string titleFromPath(const std::string& path) {
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
      if (!isIgnoredLibraryFolder(name) && rootFolderCount < UINT16_MAX) {
        ++rootFolderCount;
      }
    } else {
      std::string_view filename{name};
      if (FsHelpers::hasEpubExtension(filename) && rootEpubCount < UINT16_MAX) {
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
  return name == "XTCache" || name == ".crosspoint";
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
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, 1, radius, true);
  if (selected) {
    const Rect focus = insetRect(card, CARD_FOCUS_INSET);
    renderer.drawRoundedRect(focus.x, focus.y, focus.width, focus.height, 2, std::max(3, radius - 1), true);
  }
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

bool drawCachedCover(GfxRenderer& renderer, const std::string& coverPath, const Rect& rect) {
  if (coverPath.empty()) return false;

  FsFile file;
  if (!Storage.openFileForRead("FBA", coverPath, file)) {
    return false;
  }
  Bitmap bitmap(file);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0;
  if (ok) {
    float cropX = 0.0f;
    float cropY = 0.0f;
    RecentBooksGrid::calculateCoverFillCrop(bitmap, cropX, cropY);
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, RecentBooksGrid::kCoverCornerRadius,
                             Color::White);
    renderer.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height, cropX, cropY);
    renderer.maskRoundedRectOutsideCorners(rect.x, rect.y, rect.width, rect.height,
                                           RecentBooksGrid::kCoverCornerRadius, Color::White);
    renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, RecentBooksGrid::kCoverCornerRadius, true);
  }
  file.close();
  return ok;
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

class BookActionsActivity final : public Activity {
  struct Item {
    BookAction action;
    StrId label;
  };

  std::vector<Item> items;
  std::string title;
  int selectedIndex = 0;
  ButtonNavigator navigator;

 public:
  BookActionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookTitle,
                      const bool isToRead, const bool isFinished)
      : Activity("BookActions", renderer, mappedInput), title(std::move(bookTitle)) {
    items.reserve(5);
    items.push_back({BOOK_ACTION_CONTINUE, StrId::STR_CONTINUE_READING});
    items.push_back({BOOK_ACTION_MARK_TO_READ, isToRead ? StrId::STR_REMOVE_FROM_TO_READ : StrId::STR_MARK_TO_READ});
    items.push_back({BOOK_ACTION_MARK_FINISHED, isFinished ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
    items.push_back({BOOK_ACTION_REMOVE_STATE, StrId::STR_REMOVE_READING_STATE});
    items.push_back({BOOK_ACTION_DELETE, StrId::STR_DELETE_BOOK});
  }

  void onEnter() override {
    Activity::onEnter();
    requestUpdate(true);
  }

  void loop() override {
    navigator.onNext([this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(items.size()));
      requestUpdate();
    });
    navigator.onPrevious([this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(items.size()));
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      setResult(MenuResult{static_cast<int>(items[selectedIndex].action), 0, 0});
      finish();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      result.data = MenuResult{-1, 0, 0};
      setResult(std::move(result));
      finish();
      return;
    }
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const std::string header = title.empty() ? std::string(tr(STR_BOOK_ACTIONS)) : title;
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOK_ACTIONS));

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int innerX = metrics.contentSidePadding;
    const int innerW = pageWidth - metrics.contentSidePadding * 2;
    const int buttonTop = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const Rect panel{innerX, contentTop, innerW, buttonTop - contentTop - metrics.verticalSpacing};
    drawContainedCard(renderer, panel, false, 7);

    const int panelPad = 14;
    const int titleX = panel.x + panelPad;
    const int titleW = panel.width - panelPad * 2;
    const std::string safeTitle = renderer.truncatedText(SMALL_FONT_ID, header.c_str(), titleW, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, titleX, panel.y + panelPad, safeTitle.c_str(), true, EpdFontFamily::BOLD);

    constexpr int rowHeight = 40;
    const int listTop = panel.y + panelPad + renderer.getLineHeight(SMALL_FONT_ID) + 12;
    const int rowX = panel.x + 8;
    const int rowW = panel.width - 16;
    for (int index = 0; index < static_cast<int>(items.size()); ++index) {
      const int rowY = listTop + index * rowHeight;
      if (rowY + rowHeight > panel.y + panel.height - 8) break;
      const bool selected = index == selectedIndex;
      if (selected) {
        renderer.drawRoundedRect(rowX, rowY, rowW, rowHeight - 6, 2, 5, true);
      }
      const std::string label = renderer.truncatedText(UI_10_FONT_ID, I18N.get(items[index].label), rowW - 24);
      renderer.drawText(UI_10_FONT_ID, rowX + 12, rowY + 7, label.c_str(), true);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  }
};

class SortViewActivity final : public Activity {
  struct Item {
    SortViewAction action;
    std::string label;
    bool selected = false;
  };

  std::vector<Item> items;
  int selectedIndex = 0;
  ButtonNavigator navigator;
  bool settingsChanged = false;
  bool layoutChanged = false;

  void rebuildItems() {
    items = {
        {SORT_VIEW_SORT, std::string(tr(STR_SORT_BY)) + ": " + librarySortLabel(), false},
        {SORT_VIEW_DIRECTION, std::string(tr(STR_SORT_DIRECTION)) + ": " + librarySortDirectionLabel(), false},
        {SORT_VIEW_FILTER, std::string(tr(STR_FILTER)) + ": " + libraryFilterLabel(), false},
        {SORT_VIEW_COLUMNS, std::string(tr(STR_LIBRARY_COLUMNS)) + ": " + libraryLayoutLabel(), false},
        {SORT_VIEW_TO_READ_SHELF, std::string(tr(STR_SHOW_TO_READ_SHELF)) + ": " + libraryToReadShelfLabel(), false},
        {SORT_VIEW_REFRESH_LIBRARY, tr(STR_REFRESH_LIBRARY), false},
        {SORT_VIEW_BROWSE_FILES, tr(STR_BROWSE_ALL_FILES), false},
    };
  }

 public:
  explicit SortViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SortView", renderer, mappedInput) {}

  void onEnter() override {
    Activity::onEnter();
    rebuildItems();
    requestUpdate();
  }

  void loop() override {
    navigator.onNext([this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(items.size()));
      requestUpdate();
    });
    navigator.onPrevious([this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(items.size()));
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (items[selectedIndex].action == SORT_VIEW_SORT) {
        cycleLibrarySortSetting();
        SETTINGS.saveToFile();
        settingsChanged = true;
        rebuildItems();
        requestUpdate();
        return;
      }
      if (items[selectedIndex].action == SORT_VIEW_DIRECTION) {
        librarySortDescending = !librarySortDescending;
        settingsChanged = true;
        rebuildItems();
        requestUpdate();
        return;
      }
      if (items[selectedIndex].action == SORT_VIEW_COLUMNS) {
        switch (SETTINGS.bookshelfColumns) {
          case CrossPointSettings::BOOKSHELF_LAYOUT_2X2:
            SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_LAYOUT_3X3;
            break;
          case CrossPointSettings::BOOKSHELF_LAYOUT_3X3:
            SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_LAYOUT_3X4;
            break;
          case CrossPointSettings::BOOKSHELF_LAYOUT_3X4:
          default:
            SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_LAYOUT_2X2;
            break;
        }
        SETTINGS.saveToFile();
        settingsChanged = true;
        layoutChanged = true;
        rebuildItems();
        requestUpdate();
        return;
      }
      if (items[selectedIndex].action == SORT_VIEW_FILTER) {
        libraryFilterMode = (libraryFilterMode + 1) % 3;
        settingsChanged = true;
        rebuildItems();
        requestUpdate();
        return;
      }
      if (items[selectedIndex].action == SORT_VIEW_TO_READ_SHELF) {
        libraryShowToReadShelf = !libraryShowToReadShelf;
        settingsChanged = true;
        rebuildItems();
        requestUpdate();
        return;
      }
      setResult(MenuResult{static_cast<int>(items[selectedIndex].action), 0, 0});
      finish();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = !settingsChanged;
      result.data = MenuResult{settingsChanged ? static_cast<int>(SORT_VIEW_REFRESH) : -1,
                               static_cast<uint8_t>(layoutChanged ? 1 : 0), 0};
      setResult(std::move(result));
      finish();
      return;
    }
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SORT_BY));

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, items.size(), selectedIndex,
        [this](int index) { return items[index].label; }, nullptr, nullptr,
        [this](int index) { return items[index].selected ? std::string(tr(STR_SELECTED)) : std::string(); }, true);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  }
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
      // Check if both are at the start of a number
      if (isdigit(*s1) && isdigit(*s2)) {
        // Skip leading zeros and track them
        const char* start1 = s1;
        const char* start2 = s2;
        while (*s1 == '0') s1++;
        while (*s2 == '0') s2++;

        // Count digits to compare lengths first
        int len1 = 0, len2 = 0;
        while (isdigit(s1[len1])) len1++;
        while (isdigit(s2[len2])) len2++;

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
        char c1 = tolower(*s1);
        char c2 = tolower(*s2);
        if (c1 != c2) return c1 < c2;
        s1++;
        s2++;
      }
    }

    // One string is prefix of other
    return *s1 == '\0' && *s2 != '\0';
  });
}

void FileBrowserActivity::loadFiles() {
  files.clear();
  completedFileStates.clear();
  progressFileStates.clear();
  libraryFileStates.clear();
  folderItemCounts.clear();
  entryPaths.clear();
  entryTitles.clear();
  entrySubtitles.clear();
  entryCoverPaths.clear();
  entryCoverSourcePaths.clear();
  entryCoverStates.clear();
  libraryScanFolders.clear();
  libraryScanOffsets.clear();
  libraryCurrentScanFolder.clear();
  libraryIndexingActive = false;

  if (isBookshelfMode() && rawFilesLaunch) {
    libraryView = LIBRARY_VIEW_FILES;
  }
  if (isBookshelfMode() && basepath == "/" && libraryView == 0) {
    libraryView = LIBRARY_VIEW_DASHBOARD;
  }
  if (isBookshelfMode() && basepath == "/" && isLibraryDashboard()) {
    loadLibraryDashboard();
    clampSelector();
    return;
  }
  if (isBookshelfMode() && basepath == "/" && isLibraryShelf()) {
    loadLibraryShelf(libraryView);
    clampSelector();
    return;
  }

  loadFilesystemFiles();
  clampSelector();
}

void FileBrowserActivity::loadFilesystemFiles() {
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
  libraryFileStates.reserve(files.size());
  folderItemCounts.reserve(files.size());

  for (const auto& entry : files) {
    if (entry.empty() || entry.back() == '/') {
      completedFileStates.push_back(0);
      progressFileStates.push_back(0);
      libraryFileStates.push_back(LIBRARY_STATE_UNREAD);
      folderItemCounts.push_back(countFolderItems(entry));
      entryPaths.push_back(fullPathPrefix + entry);
      entryTitles.push_back(getFileName(entry));
      entrySubtitles.emplace_back();
      addEntryCoverPlaceholder();
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
  }
}

bool FileBrowserActivity::isLibraryDashboard() const { return libraryView == LIBRARY_VIEW_DASHBOARD; }

bool FileBrowserActivity::isLibraryShelf() const {
  return libraryView == LIBRARY_VIEW_CONTINUE || libraryView == LIBRARY_VIEW_TO_READ ||
         libraryView == LIBRARY_VIEW_FINISHED;
}

bool FileBrowserActivity::isRawBrowseFilesMode() const {
  return rawFilesLaunch || (isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES);
}

bool FileBrowserActivity::usesBookshelfGrid() const { return isBookshelfMode() && !isRawBrowseFilesMode(); }

void FileBrowserActivity::clampSelector() {
  if (files.empty()) {
    selectorIndex = 0;
  } else if (selectorIndex >= files.size()) {
    selectorIndex = files.size() - 1;
  }
}

void FileBrowserActivity::loadLibraryDashboard() {
  if (isBookshelfMode()) {
    if (librarySafeMode) {
      libraryView = LIBRARY_VIEW_FILES;
      rawFilesLaunch = true;
      loadFilesystemFiles();
      clampSelector();
      return;
    }
    recordLibraryBreadcrumb("load dashboard", basepath, "", static_cast<int>(selectorIndex),
                            static_cast<int>(files.size()));
    const bool restoredSnapshot = restoreLibraryDashboardSnapshot();
    const bool rootChanged =
        restoredSnapshot && libraryFilterMode == LIBRARY_FILTER_ALL && isLibraryRootSignatureChanged();
    if (libraryScanRequested || !restoredSnapshot || files.empty() || rootChanged) {
      startLibraryIndexing();
      libraryScanRequested = false;
    } else {
      libraryIndexingActive = false;
      libraryCurrentScanFolder.clear();
      if (!Storage.exists(LIBRARY_DASHBOARD_SIGNATURE_FILE) && libraryFilterMode == LIBRARY_FILTER_ALL) {
        saveLibraryRootSignature();
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
    libraryFileStates.push_back(LIBRARY_STATE_UNREAD);
    const bool isFolder = !entry.empty() && entry.back() == '/';
    folderItemCounts.push_back(isFolder ? countFolderItems(entry) : 0);
    entryPaths.push_back("/" + entry);
    entryTitles.push_back(getFileName(entry));
    entrySubtitles.emplace_back(isFolder ? "" : fileTypeLabel(entry));
    addEntryCoverPlaceholder();
  }
}

bool FileBrowserActivity::restoreLibraryDashboardSnapshot() {
  const auto& snapshot = libraryDashboardSnapshot();
  if (snapshot.valid) {
    files = snapshot.files;
    completedFileStates = snapshot.completedFileStates;
    progressFileStates = snapshot.progressFileStates;
    libraryFileStates = snapshot.libraryFileStates;
    folderItemCounts = snapshot.folderItemCounts;
    entryPaths = snapshot.entryPaths;
    entryTitles = snapshot.entryTitles;
    entrySubtitles = snapshot.entrySubtitles;
    entryCoverPaths = snapshot.entryCoverPaths;
    entryCoverSourcePaths = snapshot.entryCoverSourcePaths;
    entryCoverStates = snapshot.entryCoverStates;
    clampSelector();
    return true;
  }

  return restoreLibraryDashboardIndex();
}

bool FileBrowserActivity::isBadLibraryPath(const std::string& path) const {
  return std::find(libraryBadPaths.begin(), libraryBadPaths.end(), path) != libraryBadPaths.end();
}

void FileBrowserActivity::markBadLibraryPath(const std::string& path) {
  if (!path.empty() && !isBadLibraryPath(path)) {
    libraryBadPaths.push_back(path);
  }
  if (libraryFailureCount < 255) {
    ++libraryFailureCount;
  }
}

void FileBrowserActivity::recordLibraryBreadcrumb(const char* phase, const std::string& path,
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
  body += "\nindex=" + std::to_string(index);
  body += "\ncount=" + std::to_string(count);
  body += "\nlayout=" + std::to_string(static_cast<int>(SETTINGS.bookshelfColumns));
  body += "\nindexing=" + std::to_string(libraryIndexingActive ? 1 : 0);
  body += "\nthumbs=" + std::to_string(usesBookshelfGrid() ? 1 : 0);
  body += "\nfailures=" + std::to_string(static_cast<int>(libraryFailureCount));
  body += "\nheap=" + std::to_string(static_cast<unsigned long>(ESP.getFreeHeap()));
  Storage.writeFile(LIBRARY_BREADCRUMB_FILE, String(body.c_str()));
}

void FileBrowserActivity::clearLibraryBreadcrumb() const {
  Storage.mkdir("/.crosspoint");
  Storage.writeFile(LIBRARY_BREADCRUMB_FILE, String("phase=closed\npath=\nbook=\nindex=-1\ncount=0"));
}

bool FileBrowserActivity::shouldEnterLibrarySafeMode() const {
  if (rawFilesLaunch || basepath != "/" || !isBookshelfMode() || !Storage.exists(LIBRARY_BREADCRUMB_FILE)) {
    return false;
  }
  const String breadcrumb = Storage.readFile(LIBRARY_BREADCRUMB_FILE);
  if (breadcrumb.length() == 0) {
    return false;
  }
  return breadcrumb.indexOf("phase=closed") < 0 && breadcrumb.indexOf("phase=safe-mode") < 0;
}

void FileBrowserActivity::saveLibraryDashboardSnapshot() const {
  if (!isLibraryDashboard() || basepath != "/" || files.empty()) {
    return;
  }

  auto& snapshot = libraryDashboardSnapshot();
  snapshot.files = files;
  snapshot.completedFileStates = completedFileStates;
  snapshot.progressFileStates = progressFileStates;
  snapshot.libraryFileStates = libraryFileStates;
  snapshot.folderItemCounts = folderItemCounts;
  snapshot.entryPaths = entryPaths;
  snapshot.entryTitles = entryTitles;
  snapshot.entrySubtitles = entrySubtitles;
  snapshot.entryCoverPaths = entryCoverPaths;
  snapshot.entryCoverSourcePaths = entryCoverSourcePaths;
  snapshot.entryCoverStates = entryCoverStates;
  snapshot.valid = true;
}

bool FileBrowserActivity::restoreLibraryDashboardIndex() {
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
    if (path.empty() || pathHasIgnoredLibraryFolder(path) || !FsHelpers::hasEpubExtension(path)) {
      continue;
    }
    const uint8_t completed = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[7].c_str()))));
    const uint8_t progress = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[6].c_str()))));
    const uint8_t state = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[8].c_str()))));
    uint8_t coverState = static_cast<uint8_t>(std::min(255, std::max(0, atoi(fields[9].c_str()))));
    std::string coverPath = fields[4];
    if (coverState == ENTRY_COVER_READY && (coverPath.empty() || !Storage.exists(coverPath.c_str()))) {
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
    completedFileStates.push_back(completed);
    progressFileStates.push_back(progress);
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

void FileBrowserActivity::saveLibraryDashboardIndex() const {
  if (!isLibraryDashboard() || basepath != "/" || files.empty() || libraryFilterMode != LIBRARY_FILTER_ALL) {
    return;
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
                                    completedFileStates.size(), progressFileStates.size(), libraryFileStates.size()});
  for (size_t index = 0; index < rowCount; ++index) {
    const std::string& path = entryPaths[index];
    if (path.empty() || pathHasIgnoredLibraryFolder(path) || libraryFileStates[index] >= LIBRARY_STATE_SECTION ||
        !FsHelpers::hasEpubExtension(path)) {
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
    file.print('\n');
  }
  file.close();

  if (Storage.exists(LIBRARY_DASHBOARD_INDEX_FILE) && !Storage.remove(LIBRARY_DASHBOARD_INDEX_FILE)) {
    Storage.remove(LIBRARY_DASHBOARD_INDEX_TMP);
    return;
  }
  Storage.rename(LIBRARY_DASHBOARD_INDEX_TMP, LIBRARY_DASHBOARD_INDEX_FILE);
}

bool FileBrowserActivity::isLibraryRootSignatureChanged() const {
  if (!Storage.exists(LIBRARY_DASHBOARD_SIGNATURE_FILE)) {
    return false;
  }
  const String saved = Storage.readFile(LIBRARY_DASHBOARD_SIGNATURE_FILE);
  if (saved.length() == 0) {
    return false;
  }
  const std::string current = computeLibraryRootSignature();
  return !current.empty() && std::string(saved.c_str()) != current;
}

void FileBrowserActivity::saveLibraryRootSignature() const {
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

void FileBrowserActivity::startLibraryIndexing() {
  if (librarySafeMode || !usesBookshelfGrid() || !isLibraryDashboard()) {
    libraryIndexingActive = false;
    return;
  }
  recordLibraryBreadcrumb("start indexing", basepath, "", static_cast<int>(selectorIndex),
                          static_cast<int>(files.size()));
  libraryScanFolders.clear();
  libraryScanOffsets.clear();
  libraryCurrentScanFolder.clear();
  librarySkippedFolderName.clear();
  librarySkippedFolderShown = false;
  libraryScanFolders.push_back("/");
  libraryScanOffsets.push_back(0);
  libraryIndexingActive = true;
}

void FileBrowserActivity::addLibraryBookByPath(const std::string& path) {
  if (path.empty() || pathHasIgnoredLibraryFolder(path) || !FsHelpers::hasEpubExtension(path) || isBadLibraryPath(path) ||
      libraryFailureCount >= MAX_LIBRARY_FAILURES) {
    return;
  }
  recordLibraryBreadcrumb("resolve metadata", basepath, path, static_cast<int>(files.size()),
                          static_cast<int>(files.size()));
  const auto* statsBook = READING_STATS.findBook(path);
  const auto* metadata = LIBRARY_METADATA.findBook(path);
  uint8_t state = LIBRARY_STATE_UNREAD;
  const uint8_t progress = statsBook != nullptr ? statsBook->lastProgressPercent : 0;
  if ((metadata != nullptr && metadata->finished) || (statsBook != nullptr && statsBook->completed) ||
      pathHasFinishedFolder(path)) {
    state = LIBRARY_STATE_FINISHED;
  } else if (metadata != nullptr && metadata->toRead && progress < MEANINGFUL_PROGRESS_PERCENT) {
    state = LIBRARY_STATE_TO_READ;
  } else if (metadata != nullptr && metadata->pinned) {
    state = LIBRARY_STATE_PINNED;
  } else if (progress >= MEANINGFUL_PROGRESS_PERCENT && (metadata == nullptr || !metadata->activeRemoved)) {
    state = LIBRARY_STATE_READING;
  }

  addLibraryBook(path, statsBook != nullptr ? statsBook->title : "", statsBook != nullptr ? statsBook->author : "",
                 statsBook != nullptr ? statsBook->coverBmpPath : "", progress, state);
}

void FileBrowserActivity::sortLibraryDashboardBooks() {
  if (!isLibraryDashboard() || files.empty()) {
    return;
  }
  recordLibraryBreadcrumb("sort", basepath, selectorIndex < entryPaths.size() ? entryPaths[selectorIndex] : "",
                          static_cast<int>(selectorIndex),
                          static_cast<int>(files.size()));
  const auto expectedSize = files.size();
  const bool validBefore = completedFileStates.size() == expectedSize && progressFileStates.size() == expectedSize &&
                           libraryFileStates.size() == expectedSize && folderItemCounts.size() == expectedSize &&
                           entryPaths.size() == expectedSize && entryTitles.size() == expectedSize &&
                           entrySubtitles.size() == expectedSize && entryCoverPaths.size() == expectedSize &&
                           entryCoverSourcePaths.size() == expectedSize && entryCoverStates.size() == expectedSize;
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
  };

  std::vector<Row> rows;
  rows.reserve(files.size());
  for (size_t index = 0; index < files.size(); ++index) {
    const uint8_t state = libraryFileStates[index];
    Row row{files[index],
            completedFileStates[index],
            progressFileStates[index],
            state,
            folderItemCounts[index],
            entryPaths[index],
            entryTitles[index],
            entrySubtitles[index],
            entryCoverPaths[index],
            entryCoverSourcePaths[index],
            entryCoverStates[index]};
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
      rows.push_back(std::move(row));
    }
  }

  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    if (libraryFilterMode == LIBRARY_FILTER_ALL) {
      const bool toReadA = a.state == LIBRARY_STATE_TO_READ;
      const bool toReadB = b.state == LIBRARY_STATE_TO_READ;
      if (toReadA != toReadB) return toReadA;
    }
    const auto* statsA = READING_STATS.findBook(a.path);
    const auto* statsB = READING_STATS.findBook(b.path);
    const std::string titleA = !a.title.empty() ? a.title : titleFromPath(a.path);
    const std::string titleB = !b.title.empty() ? b.title : titleFromPath(b.path);
    const std::string titleKeyA = lowercaseCopy(titleA);
    const std::string titleKeyB = lowercaseCopy(titleB);
    auto compareTitle = [&]() { return librarySortDescending ? titleKeyA > titleKeyB : titleKeyA < titleKeyB; };
    switch (SETTINGS.librarySort) {
      case CrossPointSettings::LIBRARY_SORT_AUTHOR: {
        const std::string authorA = authorSortKey(a.subtitle, a.path, titleA);
        const std::string authorB = authorSortKey(b.subtitle, b.path, titleB);
        if (authorA != authorB) {
          return librarySortDescending ? authorA > authorB : authorA < authorB;
        }
        return titleKeyA < titleKeyB;
      }
      case CrossPointSettings::LIBRARY_SORT_RECENT: {
        const uint32_t recentA = statsA != nullptr ? statsA->lastReadAt : 0;
        const uint32_t recentB = statsB != nullptr ? statsB->lastReadAt : 0;
        if (recentA != recentB) return recentA > recentB;
        return titleKeyA < titleKeyB;
      }
      case CrossPointSettings::LIBRARY_SORT_PROGRESS:
        if (a.progress != b.progress) return a.progress > b.progress;
        return titleKeyA < titleKeyB;
      case CrossPointSettings::LIBRARY_SORT_TITLE:
      default:
        return compareTitle();
    }
  });

  std::vector<std::string> sortedFiles;
  std::vector<uint8_t> sortedCompleted;
  std::vector<uint8_t> sortedProgress;
  std::vector<uint8_t> sortedStates;
  std::vector<uint16_t> sortedFolderCounts;
  std::vector<std::string> sortedPaths;
  std::vector<std::string> sortedTitles;
  std::vector<std::string> sortedSubtitles;
  std::vector<std::string> sortedCoverPaths;
  std::vector<std::string> sortedCoverSourcePaths;
  std::vector<uint8_t> sortedCoverStates;
  sortedFiles.reserve(rows.size());
  sortedCompleted.reserve(rows.size());
  sortedProgress.reserve(rows.size());
  sortedStates.reserve(rows.size());
  sortedFolderCounts.reserve(rows.size());
  sortedPaths.reserve(rows.size());
  sortedTitles.reserve(rows.size());
  sortedSubtitles.reserve(rows.size());
  sortedCoverPaths.reserve(rows.size());
  sortedCoverSourcePaths.reserve(rows.size());
  sortedCoverStates.reserve(rows.size());

  for (auto& row : rows) {
    sortedFiles.push_back(std::move(row.file));
    sortedCompleted.push_back(row.completed);
    sortedProgress.push_back(row.progress);
    sortedStates.push_back(row.state);
    sortedFolderCounts.push_back(row.folderCount);
    sortedPaths.push_back(std::move(row.path));
    sortedTitles.push_back(std::move(row.title));
    sortedSubtitles.push_back(std::move(row.subtitle));
    sortedCoverPaths.push_back(std::move(row.coverPath));
    sortedCoverSourcePaths.push_back(std::move(row.coverSourcePath));
    sortedCoverStates.push_back(row.coverState);
  }

  const auto sortedSize = sortedFiles.size();
  const bool validAfter = sortedCompleted.size() == sortedSize && sortedProgress.size() == sortedSize &&
                          sortedStates.size() == sortedSize && sortedFolderCounts.size() == sortedSize &&
                          sortedPaths.size() == sortedSize && sortedTitles.size() == sortedSize &&
                          sortedSubtitles.size() == sortedSize && sortedCoverPaths.size() == sortedSize &&
                          sortedCoverSourcePaths.size() == sortedSize && sortedCoverStates.size() == sortedSize;
  if (!validAfter) {
    recordLibraryBreadcrumb("sort_validation_failed", basepath, "", static_cast<int>(selectorIndex),
                            static_cast<int>(files.size()));
    clampSelector();
    return;
  }

  files = std::move(sortedFiles);
  completedFileStates = std::move(sortedCompleted);
  progressFileStates = std::move(sortedProgress);
  libraryFileStates = std::move(sortedStates);
  folderItemCounts = std::move(sortedFolderCounts);
  entryPaths = std::move(sortedPaths);
  entryTitles = std::move(sortedTitles);
  entrySubtitles = std::move(sortedSubtitles);
  entryCoverPaths = std::move(sortedCoverPaths);
  entryCoverSourcePaths = std::move(sortedCoverSourcePaths);
  entryCoverStates = std::move(sortedCoverStates);
  clampSelector();
  if (libraryFilterMode == LIBRARY_FILTER_ALL) {
    saveLibraryDashboardSnapshot();
  }
}

bool FileBrowserActivity::processLibraryIndexJob() {
  if (!libraryIndexingActive || librarySafeMode || !usesBookshelfGrid() || !isLibraryDashboard() ||
      mappedInput.isAnyMappedButtonPressed() || libraryRendering || libraryWorkPaused) {
    return false;
  }
  if (libraryFailureCount >= MAX_LIBRARY_FAILURES) {
    libraryIndexingActive = false;
    recordLibraryBreadcrumb("indexing stopped after failures", basepath, "", static_cast<int>(selectorIndex),
                            static_cast<int>(files.size()));
    return false;
  }
  const unsigned long now = millis();
  if (!libraryFirstRenderDone || now - lastLibraryRenderFinishedMs < LIBRARY_POST_RENDER_WORK_IDLE_MS ||
      now - libraryEnteredAtMs < 250 ||
      now - lastNavigationInputMs < LIBRARY_BACKGROUND_IDLE_MS || now - lastLibraryWorkMs < 120) {
    return false;
  }
  lastLibraryWorkMs = now;
  if (libraryScanFolders.empty() || files.size() >= MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT) {
    libraryIndexingActive = false;
    recordLibraryBreadcrumb("finish indexing", basepath, "", static_cast<int>(selectorIndex),
                            static_cast<int>(files.size()));
    sortLibraryDashboardBooks();
    saveLibraryDashboardSnapshot();
    saveLibraryDashboardIndex();
    saveLibraryRootSignature();
    clampSelector();
    requestUpdate();
    return false;
  }

  const std::string folderPath = libraryScanFolders.back();
  const size_t startOffset = libraryScanOffsets.empty() ? 0 : libraryScanOffsets.back();
  libraryCurrentScanFolder = folderPath;
  libraryScanFolders.pop_back();
  if (!libraryScanOffsets.empty()) {
    libraryScanOffsets.pop_back();
  }
  recordLibraryBreadcrumb("scan folder", folderPath, "", static_cast<int>(selectorIndex),
                          static_cast<int>(files.size()));
  const int depth = static_cast<int>(std::count(folderPath.begin(), folderPath.end(), '/'));
  if (depth > MAX_LIBRARY_SCAN_DEPTH) {
    return false;
  }

  auto dir = Storage.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    markBadLibraryPath(folderPath);
    return false;
  }

  bool addedBook = false;
  int filesProcessed = 0;
  int foldersQueued = 0;
  size_t entryOffset = 0;
  char name[500];
  for (auto file = dir.openNextFile(); file && files.size() < MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT;
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
      if (isIgnoredLibraryFolder(name) || pathHasIgnoredLibraryFolder(childPath)) {
        if (librarySkippedFolderName.empty()) {
          librarySkippedFolderName = getFileName(name);
          librarySkippedFolderShown = false;
        }
        continue;
      }
      if (isBadLibraryPath(childPath)) {
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

    std::string_view filename{name};
    if (FsHelpers::hasEpubExtension(filename)) {
      const size_t before = files.size();
      recordLibraryBreadcrumb("add book", folderPath, childPath, static_cast<int>(before),
                              static_cast<int>(files.size()));
      if (!isBadLibraryPath(childPath)) {
        addLibraryBookByPath(childPath);
      }
      addedBook = addedBook || files.size() != before;
      ++filesProcessed;
    }
    file.close();
    if (filesProcessed >= MAX_LIBRARY_FILES_PER_TICK || foldersQueued >= MAX_LIBRARY_FOLDERS_PER_TICK) {
      libraryScanFolders.push_back(folderPath);
      libraryScanOffsets.push_back(entryOffset);
      break;
    }
  }
  dir.close();
  if (addedBook) {
    clampSelector();
    saveLibraryDashboardSnapshot();
    requestUpdate();
  }
  return addedBook;
}

void FileBrowserActivity::addLibraryBook(const std::string& path, const std::string& title, const std::string& author,
                                         const std::string& coverPath, const uint8_t progress, const uint8_t state) {
  if (path.empty()) return;
  for (const auto& existingPath : entryPaths) {
    if (existingPath == path) return;
  }

  files.push_back(path);
  entryPaths.push_back(path);
  addEntryCoverPlaceholder();
  entryCoverSourcePaths.back() = coverPath;
  const size_t slash = path.find_last_of('/');
  const std::string fallbackName = slash == std::string::npos ? path : path.substr(slash + 1);
  entryTitles.push_back(title.empty() ? getFileName(fallbackName) : title);
  entrySubtitles.push_back(author);
  completedFileStates.push_back(state == LIBRARY_STATE_FINISHED ? 1 : 0);
  progressFileStates.push_back(progress);
  libraryFileStates.push_back(state);
  folderItemCounts.push_back(0);
}

void FileBrowserActivity::loadLibraryShelf(const uint8_t shelf) {
  for (const auto& book : READING_STATS.getBooks()) {
    if (book.path.empty()) continue;
    const auto* metadata = LIBRARY_METADATA.findBook(book.path);
    if (metadata != nullptr && metadata->toRead && book.lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT) {
      LIBRARY_METADATA.removeFromToRead(book.path);
      metadata = LIBRARY_METADATA.findBook(book.path);
    }
    const bool metadataFinished = metadata != nullptr && metadata->finished;
    const bool folderFinished = pathHasFinishedFolder(book.path);
    const bool activeRemoved = metadata != nullptr && metadata->activeRemoved;
    const bool toRead = metadata != nullptr && metadata->toRead &&
                        book.lastProgressPercent < MEANINGFUL_PROGRESS_PERCENT;
    uint8_t state = (book.completed || metadataFinished || folderFinished)
                        ? LIBRARY_STATE_FINISHED
                        : (book.lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT ? LIBRARY_STATE_READING : LIBRARY_STATE_UNREAD);

    if (toRead) state = LIBRARY_STATE_TO_READ;

    const bool include =
        (shelf == LIBRARY_VIEW_CONTINUE && !book.completed && !metadataFinished && !folderFinished && !toRead && !activeRemoved &&
         book.lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT) ||
        (shelf == LIBRARY_VIEW_FINISHED && (book.completed || metadataFinished || folderFinished)) ||
        (shelf == LIBRARY_VIEW_TO_READ && toRead && !metadataFinished);
    if (include) {
      addLibraryBook(book.path, book.title, book.author, book.coverBmpPath, book.lastProgressPercent, state);
    }
  }

  if (shelf == LIBRARY_VIEW_TO_READ || shelf == LIBRARY_VIEW_FINISHED) {
    for (const auto& book : LIBRARY_METADATA.getBooks()) {
      const bool include = (shelf == LIBRARY_VIEW_TO_READ && book.toRead && !book.finished) ||
                           (shelf == LIBRARY_VIEW_FINISHED && book.finished);
      if (include && READING_STATS.findBook(book.path) == nullptr) {
        addLibraryBook(book.path, "", "", "", 0, book.finished ? LIBRARY_STATE_FINISHED : LIBRARY_STATE_TO_READ);
      }
    }
  }
}

void FileBrowserActivity::addEntryCoverPlaceholder() {
  entryCoverPaths.emplace_back();
  entryCoverSourcePaths.emplace_back();
  entryCoverStates.push_back(ENTRY_COVER_UNKNOWN);
}

bool FileBrowserActivity::entryCanResolveCover(const int index) const {
  if (!usesBookshelfGrid()) {
    return false;
  }
  if (index < 0 || index >= static_cast<int>(files.size()) || index >= static_cast<int>(entryPaths.size())) {
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
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path);
}

bool FileBrowserActivity::resolveEntryCover(const int index, const bool allowGeneration) {
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

  if (auto* cached = findThumbnailCacheRecord(path, sourceCoverPath, coverTargetWidth, coverTargetHeight)) {
    if (cached->state == ENTRY_COVER_READY && !cached->thumbPath.empty() && Storage.exists(cached->thumbPath.c_str())) {
      entryCoverStates[index] = ENTRY_COVER_READY;
      entryCoverPaths[index] = cached->thumbPath;
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

  const std::string generatedThumbPath =
      UITheme::ensureBookCoverThumbPath(path, sourceCoverPath, coverTargetWidth, coverTargetHeight);

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

bool FileBrowserActivity::processVisibleCoverJob(const int pageItems) {
  if (!usesBookshelfGrid() || pageItems <= 0 || mappedInput.isAnyMappedButtonPressed() || libraryRendering ||
      libraryWorkPaused) {
    return false;
  }
  if (isLibraryDashboard() && libraryIndexingActive) {
    return false;
  }
  if (ESP.getFreeHeap() < MIN_LIBRARY_COVER_HEAP || libraryFailureCount >= MAX_LIBRARY_FAILURES) {
    return false;
  }
  if (millis() - lastNavigationInputMs < LIBRARY_BACKGROUND_IDLE_MS) {
    return false;
  }
  const unsigned long now = millis();
  if (!libraryFirstRenderDone || now - lastLibraryRenderFinishedMs < LIBRARY_POST_RENDER_WORK_IDLE_MS ||
      now - libraryEnteredAtMs < 500 || now - lastLibraryWorkMs < 160) {
    return false;
  }
  lastLibraryWorkMs = now;

  const int pageStartIndex = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + pageItems);
  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    if (index < static_cast<int>(entryCoverStates.size()) && entryCoverStates[index] == ENTRY_COVER_UNKNOWN &&
        entryCanResolveCover(index)) {
      resolveEntryCover(index, true);
      requestUpdate();
      return true;
    }
  }
  if (isLibraryDashboard()) {
    for (int index = 0; index < static_cast<int>(files.size()); ++index) {
      if (index >= pageStartIndex && index < pageEndIndex) {
        continue;
      }
      if (index < static_cast<int>(entryCoverStates.size()) && entryCoverStates[index] == ENTRY_COVER_UNKNOWN &&
          entryCanResolveCover(index)) {
        resolveEntryCover(index, true);
        requestUpdate();
        return true;
      }
    }
  }
  return false;
}

int FileBrowserActivity::countPendingCoverJobs(const int pageItems) const {
  if (!usesBookshelfGrid() || pageItems <= 0) {
    return 0;
  }
  const int pageStartIndex = isLibraryDashboard() ? 0 : (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEndIndex =
      isLibraryDashboard() ? static_cast<int>(files.size()) : std::min(static_cast<int>(files.size()), pageStartIndex + pageItems);
  int pending = 0;
  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    if (index < static_cast<int>(entryCoverStates.size()) && entryCoverStates[index] == ENTRY_COVER_UNKNOWN &&
        entryCanResolveCover(index)) {
      ++pending;
    }
  }
  return pending;
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();
  lastNavigationInputMs = millis();
  libraryEnteredAtMs = millis();
  lastLibraryWorkMs = millis();
  lastLibraryRenderFinishedMs = millis();
  libraryFirstRenderDone = false;
  libraryRendering = false;
  libraryWorkPaused = false;
  libraryScanRequested = false;
  librarySkippedFolderName.clear();
  librarySkippedFolderShown = false;
  libraryBreadcrumbClearedThisSession = false;

  if (isBookshelfMode() && rawFilesLaunch) {
    libraryView = LIBRARY_VIEW_FILES;
  }
  if (!rawFilesLaunch && basepath == "/" && shouldEnterLibrarySafeMode()) {
    librarySafeMode = true;
    recordLibraryBreadcrumb("safe-mode", "/", "", 0, 0);
    rawFilesLaunch = true;
    libraryView = LIBRARY_VIEW_FILES;
  }

  if (!rawFilesLaunch && hasSavedBrowserCursor && basepath == "/") {
    basepath = savedBrowserBasepath;
    libraryView = savedBrowserLibraryView;
    selectorIndex = savedBrowserSelectorIndex;
  } else {
    selectorIndex = 0;
  }
  confirmLongPressHandled = false;
  backLongPressHandled = false;
  holdPreviewVisible = false;

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

void FileBrowserActivity::onExit() {
  Activity::onExit();
  libraryWorkPaused = true;
  libraryRendering = false;
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
  libraryScanFolders.clear();
  libraryScanOffsets.clear();
  libraryCurrentScanFolder.clear();
  librarySkippedFolderName.clear();
  if (!rawFilesLaunch) {
    savedBrowserBasepath = basepath;
    savedBrowserLibraryView = libraryView;
    savedBrowserSelectorIndex = selectorIndex;
    hasSavedBrowserCursor = true;
  }
  files.clear();
  completedFileStates.clear();
  progressFileStates.clear();
  libraryFileStates.clear();
  folderItemCounts.clear();
  entryPaths.clear();
  entryTitles.clear();
  entrySubtitles.clear();
  entryCoverPaths.clear();
  entryCoverSourcePaths.clear();
  entryCoverStates.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
  // Only clear cache for .epub files
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
    LOG_DBG("FileBrowser", "Cleared metadata cache for: %s", fullPath.c_str());
  }
}

void FileBrowserActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    backLongPressHandled = false;
    sortPreviewVisible = false;
    libraryWorkPaused = true;
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
      isBookshelfMode() && basepath == "/" && (isLibraryDashboard() || isRawBrowseFilesMode());

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

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const int pageItems = getPageItems(contentHeight);

  const bool hasSelection = !files.empty() && selectorIndex < files.size();
  const std::string selectedEntry = hasSelection ? files[selectorIndex] : "";
  const bool selectedIsDirectory = !selectedEntry.empty() && selectedEntry.back() == '/';
  const bool selectedSupportedBook =
      hasSelection && selectorIndex < entryPaths.size() &&
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
        onSelectBook(entryPaths[selectorIndex]);
        return;
      }
      selectorIndex = 0;
      loadFiles();
      requestUpdate();
      return;
    }

    if (isBookshelfMode() && isLibraryShelf()) {
      if (selectorIndex >= entryPaths.size() || entryPaths[selectorIndex].empty()) return;
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

  if (isLibraryDashboard() && libraryFirstRenderDone && !librarySafeMode && !libraryBreadcrumbClearedThisSession &&
      !mappedInput.isAnyMappedButtonPressed()) {
    clearLibraryBreadcrumb();
    libraryBreadcrumbClearedThisSession = true;
  }
  if (!mappedInput.isAnyMappedButtonPressed() && millis() - lastNavigationInputMs >= LIBRARY_BACKGROUND_IDLE_MS) {
    libraryWorkPaused = false;
  }
  if (!processLibraryIndexJob()) {
    processVisibleCoverJob(pageItems);
  }
}

void FileBrowserActivity::openBookActions(const size_t index) {
  if (index >= files.size()) return;
  const std::string entry = files[index];
  if (!entry.empty() && entry.back() == '/') return;

  const std::string fullPath = index < entryPaths.size() && !entryPaths[index].empty() ? entryPaths[index]
                                                                                       : getFullPathForEntry(entry);
  if (fullPath.empty()) return;
  const std::string title = getEntryTitle(static_cast<int>(index));

  const bool isToRead = LIBRARY_METADATA.isToRead(fullPath);
  const auto* statsBook = READING_STATS.findBook(fullPath);
  const bool isFinished = LIBRARY_METADATA.isFinished(fullPath) || (statsBook != nullptr && statsBook->completed);
  startActivityForResult(std::make_unique<BookActionsActivity>(renderer, mappedInput, title, isToRead, isFinished),
                         [this, fullPath, title, entry](const ActivityResult& result) {
                           mappedInput.consumeActiveHoldUntilRelease();
                           confirmLongPressHandled = false;
                           holdPreviewVisible = false;
                           if (result.isCancelled || !std::holds_alternative<MenuResult>(result.data)) {
                             requestUpdate();
                             return;
                           }
                           const auto& menu = std::get<MenuResult>(result.data);
                           handleBookAction(menu.action, fullPath, title, entry);
                         });
}

void FileBrowserActivity::openSortViewMenu() {
  startActivityForResult(std::make_unique<SortViewActivity>(renderer, mappedInput), [this](const ActivityResult& result) {
    mappedInput.consumeActiveHoldUntilRelease();
    if (result.isCancelled || !std::holds_alternative<MenuResult>(result.data)) {
      requestUpdate();
      return;
    }
    const auto& menu = std::get<MenuResult>(result.data);
    const int action = menu.action;
    const bool layoutChanged = menu.orientation != 0;
    switch (static_cast<SortViewAction>(action)) {
      case SORT_VIEW_REFRESH:
        break;
      case SORT_VIEW_BROWSE_FILES:
        clearLibraryBreadcrumb();
        libraryView = LIBRARY_VIEW_FILES;
        rawFilesLaunch = true;
        selectorIndex = 0;
        loadFiles();
        requestUpdate(true);
        return;
      case SORT_VIEW_REFRESH_LIBRARY:
        libraryFilterMode = LIBRARY_FILTER_ALL;
        libraryScanRequested = true;
        libraryWorkPaused = false;
        libraryIndexingActive = false;
        libraryScanFolders.clear();
        libraryScanOffsets.clear();
        libraryCurrentScanFolder.clear();
        librarySkippedFolderName.clear();
        librarySkippedFolderShown = false;
        loadFiles();
        clampSelector();
        requestUpdate(true);
        return;
      case SORT_VIEW_SORT:
      case SORT_VIEW_DIRECTION:
      case SORT_VIEW_FILTER:
      case SORT_VIEW_TO_READ_SHELF:
      case SORT_VIEW_COLUMNS:
      default:
        break;
    }
    const std::string selectedPath =
        selectorIndex < entryPaths.size() && !entryPaths[selectorIndex].empty() ? entryPaths[selectorIndex] : "";
    loadFiles();
    if (isLibraryDashboard()) {
      sortLibraryDashboardBooks();
    }
    if (layoutChanged && libraryFilterMode == LIBRARY_FILTER_ALL) saveLibraryDashboardSnapshot();
    if (!selectedPath.empty()) {
      for (size_t index = 0; index < entryPaths.size(); ++index) {
        if (entryPaths[index] == selectedPath) {
          selectorIndex = index;
          break;
        }
      }
    }
    clampSelector();
    requestUpdate(true);
  });
}

void FileBrowserActivity::handleBookAction(const int action, const std::string& path, const std::string& title,
                                           const std::string& entry) {
  switch (static_cast<BookAction>(action)) {
    case BOOK_ACTION_CONTINUE:
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
      if (LIBRARY_METADATA.isFinished(path)) {
        LIBRARY_METADATA.removeFinishedState(path);
      } else {
        LIBRARY_METADATA.setFinished(path);
      }
      if (isLibraryDashboard()) {
        for (size_t index = 0; index < entryPaths.size(); ++index) {
          if (entryPaths[index] == path && index < libraryFileStates.size()) {
            const bool finished = LIBRARY_METADATA.isFinished(path);
            libraryFileStates[index] = finished ? LIBRARY_STATE_FINISHED : LIBRARY_STATE_UNREAD;
            completedFileStates[index] = finished ? 1 : 0;
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
    case BOOK_ACTION_REMOVE_STATE:
      LIBRARY_METADATA.removeActiveReadingState(path);
      loadFiles();
      requestUpdate(true);
      return;
    case BOOK_ACTION_DELETE:
      confirmDeleteFile(path, title.empty() ? entry : title);
      return;
  }
}

void FileBrowserActivity::confirmDeleteFile(const std::string& fullPath, const std::string& label) {
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

bool FileBrowserActivity::isBookshelfMode() const {
  return SETTINGS.uiTheme == CrossPointSettings::LYRA_VCODEX2 &&
         SETTINGS.fileBrowserView == CrossPointSettings::FILE_BROWSER_BOOKSHELF;
}

int FileBrowserActivity::getBookshelfColumns() const {
  switch (SETTINGS.bookshelfColumns) {
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X3:
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X4:
      return 3;
    case CrossPointSettings::BOOKSHELF_LAYOUT_2X2:
    default:
      return 2;
  }
}

int FileBrowserActivity::getBookshelfRows() const {
  switch (SETTINGS.bookshelfColumns) {
    case CrossPointSettings::BOOKSHELF_LAYOUT_2X2:
      return 2;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X3:
      return 3;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X4:
      return 4;
    default:
      return 2;
  }
}

int FileBrowserActivity::getBookshelfCardHeight() const {
  switch (SETTINGS.bookshelfColumns) {
    case CrossPointSettings::BOOKSHELF_LAYOUT_2X2:
      return isLibraryDashboard() ? 214 : 226;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X3:
      return 132;
    case CrossPointSettings::BOOKSHELF_LAYOUT_3X4:
      return 96;
    default:
      return isLibraryDashboard() ? 214 : 226;
  }
}

int FileBrowserActivity::getPageItems(const int contentHeight) const {
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

std::vector<int> FileBrowserActivity::getVisibleDashboardIndices() const {
  std::vector<int> indices;
  indices.reserve(files.size());
  if (!isLibraryDashboard()) {
    for (int index = 0; index < static_cast<int>(files.size()); ++index) {
      indices.push_back(index);
    }
    return indices;
  }

  const int toReadPreviewLimit = SETTINGS.bookshelfColumns == CrossPointSettings::BOOKSHELF_LAYOUT_2X2 ? 2 : 3;
  int toReadPreviewCount = 0;
  if (libraryFilterMode == LIBRARY_FILTER_ALL && libraryShowToReadShelf) {
    for (int index = 0; index < static_cast<int>(files.size()) && toReadPreviewCount < toReadPreviewLimit; ++index) {
      const uint8_t state = index < static_cast<int>(libraryFileStates.size()) ? libraryFileStates[index] : 0;
      if (state == LIBRARY_STATE_TO_READ) {
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
    indices.push_back(index);
  }
  return indices;
}

int FileBrowserActivity::getVisibleDashboardPosition(const std::vector<int>& visibleIndices) const {
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

uint16_t FileBrowserActivity::countFolderItems(const std::string& folderName) const {
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
    if (!hidden && (file.isDirectory() || visibleFile) && count < UINT16_MAX) {
      ++count;
    }
    file.close();
  }
  dir.close();
  return count;
}

std::string FileBrowserActivity::getFullPathForEntry(const std::string& entry) const {
  std::string path = basepath;
  if (path.empty() || path.back() != '/') path += "/";
  return path + entry;
}

std::string FileBrowserActivity::getLibraryStateLabel(const int index) const {
  if (index < 0 || index >= static_cast<int>(libraryFileStates.size())) {
    return "";
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

std::string FileBrowserActivity::getEntryTitle(const int index) const {
  if (index >= 0 && index < static_cast<int>(entryTitles.size()) && !entryTitles[index].empty()) {
    return entryTitles[index];
  }
  if (index >= 0 && index < static_cast<int>(files.size())) {
    return getFileName(files[index]);
  }
  return "";
}

std::string FileBrowserActivity::getEntrySubtitle(const int index) const {
  if (index >= 0 && index < static_cast<int>(entrySubtitles.size())) {
    return entrySubtitles[index];
  }
  return "";
}

void FileBrowserActivity::moveBookshelfHorizontal(const int delta) {
  if (files.empty()) return;
  lastNavigationInputMs = millis();
  libraryWorkPaused = true;
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
    clampSelector();
    requestUpdate();
    return;
  }
  const int listSize = static_cast<int>(files.size());
  const int next = delta > 0 ? ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)
                             : ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
  selectorIndex = static_cast<size_t>(next);
  clampSelector();
  requestUpdate();
}

void FileBrowserActivity::moveBookshelfVertical(const int delta) {
  if (files.empty()) return;
  lastNavigationInputMs = millis();
  libraryWorkPaused = true;
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
    clampSelector();
    requestUpdate();
    return;
  }
  const int listSize = static_cast<int>(files.size());
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
  clampSelector();
  requestUpdate();
}

void FileBrowserActivity::moveBookshelfPage(const int delta, const int pageItems) {
  if (files.empty() || pageItems <= 0) return;
  lastNavigationInputMs = millis();
  libraryWorkPaused = true;
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

void FileBrowserActivity::renderBookshelf(const Rect& rect, const int pageItems) {
  if (isLibraryDashboard()) {
    renderLibraryDashboard(rect, pageItems);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int columns = getBookshelfColumns();
  const int cardGap = BOOKSHELF_CARD_GAP;
  const int cardWidth = (rect.width - sidePadding * 2 - cardGap * (columns - 1)) / columns;
  const int pageStartIndex = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + pageItems);
  const int visibleRows = std::max(1, (pageEndIndex - pageStartIndex + columns - 1) / columns);
  const int cardHeight = std::max(SHELF_COVER_MIN_HEIGHT + COVER_GRID_PAD * 2,
                                  (rect.height - cardGap * (visibleRows - 1)) / visibleRows);

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
      const bool coverShelf = isLibraryShelf();
      const bool bookTile = coverShelf || basepath != "/";
      const Rect visualRect =
          bookTile ? calculateBookCoverRect(card, inner, inner.y + inner.height + 12, 0, 0)
                   : Rect{card.x + (card.width - std::min(inner.width, 58)) / 2, inner.y,
                          std::min(inner.width, 58), 52};
      const bool shouldDrawCover = bookTile && index >= 0 && index < static_cast<int>(entryCoverPaths.size());
      const bool coverDrawn = shouldDrawCover && drawCachedCover(renderer, entryCoverPaths[index], visualRect);
      if (!coverDrawn) {
        drawBookPlaceholder(renderer, visualRect, icon == Image24Icon);
      }
      if (selected) {
        const Rect focus = insetRect(visualRect, -3);
        renderer.drawRoundedRect(focus.x, focus.y, focus.width, focus.height, 2,
                                 RecentBooksGrid::kCoverCornerRadius + 1, true);
      }
    }
  }
}

void FileBrowserActivity::renderLibraryDashboard(const Rect& rect, const int pageItems) {
  if (pageItems <= 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int bookColumns = getBookshelfColumns();
  const int toReadColumns = SETTINGS.bookshelfColumns == CrossPointSettings::BOOKSHELF_LAYOUT_2X2 ? 2 : 3;
  const int cardGap = BOOKSHELF_CARD_GAP;
  const int bookCardWidth = (rect.width - sidePadding * 2 - cardGap * (bookColumns - 1)) / bookColumns;
  const auto visibleIndices = getVisibleDashboardIndices();
  if (visibleIndices.empty()) return;
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
  const bool showAllBooksHeading = pageStartPosition == 0 && firstNormalPosition == pageStartPosition;
  const bool showAllBooksAfterShelf = showToReadHeading && firstNormalPosition >= 0;
  const int headingHeight = (showToReadHeading || showAllBooksHeading) ? LIBRARY_SECTION_LABEL_HEIGHT : 0;
  const int allBooksLabelHeight = showAllBooksAfterShelf ? LIBRARY_SECTION_LABEL_HEIGHT : 0;
  const int toReadSectionExtra = showToReadHeading ? LIBRARY_TO_READ_SECTION_PAD * 2 : 0;
  const int gridTop = rect.y + headingHeight + (showToReadHeading ? 2 : 0);
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
  const int layoutItems = pageEndPosition - pageStartPosition;
  const int visibleRows = std::max(1, (layoutItems + bookColumns - 1) / bookColumns);
  const int gridHeight = std::max(SHELF_COVER_MIN_HEIGHT + COVER_GRID_PAD * 2,
                                  rect.height - headingHeight - allBooksLabelHeight - toReadSectionExtra);
  const int bookCardHeight = std::max(SHELF_COVER_MIN_HEIGHT + COVER_GRID_PAD * 2,
                                      (gridHeight - cardGap * (visibleRows - 1)) / visibleRows);

  if (showToReadHeading) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, rect.y, tr(STR_TO_READ), true, EpdFontFamily::BOLD);
    if (toReadVisible > 0) {
      const Rect toReadCard{sidePadding, rect.y + LIBRARY_SECTION_LABEL_HEIGHT + 1,
                            rect.width - sidePadding * 2,
                            toReadRows * bookCardHeight + std::max(0, toReadRows - 1) * cardGap +
                                LIBRARY_TO_READ_SECTION_PAD * 2};
      renderer.drawRoundedRect(toReadCard.x, toReadCard.y, toReadCard.width, toReadCard.height, 2, 6, true);
    }
  } else if (showAllBooksHeading) {
    renderer.drawText(SMALL_FONT_ID, sidePadding, rect.y, tr(STR_ALL_BOOKS), true, EpdFontFamily::BOLD);
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
    const Rect inner = insetRect(card, isShelf ? CARD_PAD : COVER_GRID_PAD);
    if (position == firstNormalPosition && firstNormalPosition > pageStartPosition) {
      const int dividerY = std::max(rect.y + 2, card.y - cardGap);
      if (column == 0) {
        renderer.drawText(SMALL_FONT_ID, sidePadding, std::max(rect.y, dividerY - LIBRARY_SECTION_LABEL_HEIGHT + 2),
                          tr(STR_ALL_BOOKS), true, EpdFontFamily::BOLD);
      }
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

    const Rect coverRect = calculateBookCoverRect(card, inner, inner.y + inner.height + 12, 0, 0);
    const bool coverDrawn =
        index >= 0 && index < static_cast<int>(entryCoverPaths.size()) && drawCachedCover(renderer, entryCoverPaths[index], coverRect);
    if (!coverDrawn) {
      drawBookPlaceholder(renderer, coverRect, false);
    }
    if (selected) {
      const Rect focus = insetRect(coverRect, -3);
      renderer.drawRoundedRect(focus.x, focus.y, focus.width, focus.height, 2,
                               RecentBooksGrid::kCoverCornerRadius + 1, true);
      const Rect outerFocus = insetRect(coverRect, -6);
      renderer.drawRoundedRect(outerFocus.x, outerFocus.y, outerFocus.width, outerFocus.height, 1,
                               RecentBooksGrid::kCoverCornerRadius + 3, true);
      renderer.fillRect(focus.x + 4, focus.y + focus.height - 4, std::max(0, focus.width - 8), 2, true);
    }
  }
}

void FileBrowserActivity::renderPageIndicator(const Rect& rect, const int pageItems) const {
  if (!usesBookshelfGrid() || files.empty() || pageItems <= 0) {
    return;
  }
  int visibleCount = static_cast<int>(files.size());
  int visiblePosition = static_cast<int>(selectorIndex);
  if (isLibraryDashboard()) {
    const auto visibleIndices = getVisibleDashboardIndices();
    visibleCount = static_cast<int>(visibleIndices.size());
    visiblePosition = getVisibleDashboardPosition(visibleIndices);
  }
  if (visibleCount <= pageItems) {
    return;
  }
  const int pageCount = (visibleCount + pageItems - 1) / pageItems;
  const int currentPage = std::min(pageCount, visiblePosition / pageItems + 1);
  const std::string pageLabel = std::to_string(currentPage) + "/" + std::to_string(pageCount);
  const int textW = renderer.getTextWidth(SMALL_FONT_ID, pageLabel.c_str());
  const int x = rect.x + rect.width - UITheme::getInstance().getMetrics().contentSidePadding - textW;
  const int y = rect.y + rect.height - renderer.getLineHeight(SMALL_FONT_ID) - 2;
  renderer.drawText(SMALL_FONT_ID, x, y, pageLabel.c_str(), true, EpdFontFamily::BOLD);
}

void FileBrowserActivity::render(RenderLock&&) {
  libraryRendering = true;
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  if (librarySafeMode) {
    folderName = tr(STR_LIBRARY_SAFE_MODE);
  } else if (usesBookshelfGrid() && basepath == "/") {
    if (isLibraryDashboard()) {
      folderName = tr(STR_LIBRARY);
    } else if (libraryView == LIBRARY_VIEW_TO_READ) {
      folderName = tr(STR_TO_READ);
    } else if (libraryView == LIBRARY_VIEW_FINISHED) {
      folderName = tr(STR_FINISHED_BOOKS);
    }
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  std::string headerStatusText;
  if (SETTINGS.advancedStatusHeader && usesBookshelfGrid()) {
    const int headerPageItems = getPageItems(pageHeight - metrics.topPadding - metrics.headerHeight -
                                             metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved);
    const int pendingCovers = countPendingCoverJobs(headerPageItems);
    if (libraryIndexingActive || pendingCovers > 0) {
      int discoveredBooks = 0;
      for (const auto& path : entryPaths) {
        if (!path.empty()) {
          ++discoveredBooks;
        }
      }
      if (libraryIndexingActive && !librarySkippedFolderName.empty() && !librarySkippedFolderShown) {
        headerStatusText = std::string(tr(STR_SKIPPED_FOLDER)) + ": " + librarySkippedFolderName;
      } else if (libraryIndexingActive) {
        const std::string folder = libraryCurrentScanFolder.empty() || libraryCurrentScanFolder == "/"
                                       ? std::string("/")
                                       : getFileName(libraryCurrentScanFolder);
        headerStatusText = "Scanning Library | " + folder + " | Books: " + std::to_string(discoveredBooks);
      } else {
        headerStatusText = "Covers Loaded: " + std::to_string(std::max(0, discoveredBooks - pendingCovers)) + "/" +
                           std::to_string(discoveredBooks);
      }
    }
  }
  const bool showHeaderStatus = !headerStatusText.empty();
  const int statusReserved = showHeaderStatus ? renderer.getLineHeight(SMALL_FONT_ID) + 2 : 0;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + statusReserved;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  if (files.empty()) {
    std::string emptyTitle = usesBookshelfGrid() ? tr(STR_NO_BOOKS_HERE) : tr(STR_NO_FILES_FOUND);
    std::string emptySubtitle = tr(STR_ADD_EPUB_FILES);
    if (isLibraryDashboard() && libraryIndexingActive) {
      emptyTitle = tr(STR_INDEXING);
      emptySubtitle = tr(STR_LIBRARY);
    }
    if (isLibraryShelf()) {
      if (libraryView == LIBRARY_VIEW_FINISHED) {
        emptyTitle = "No finished books yet";
      } else if (libraryView == LIBRARY_VIEW_TO_READ) {
        emptyTitle = "No To Read books";
      } else if (libraryView == LIBRARY_VIEW_CONTINUE) {
        emptyTitle = "No books in progress";
      }
      emptySubtitle = "Use Browse Files to open or mark books";
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
    renderBookshelf(contentRect, pageItems);
    renderPageIndicator(contentRect, pageItems);
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

  // Full path display
  {
    const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
    std::string infoText = librarySafeMode ? tr(STR_LIBRARY_SAFE_MODE_BODY) : basepath;
    if (usesBookshelfGrid() && !files.empty() && selectorIndex < files.size()) {
      const bool selectedBook =
          selectorIndex < entryPaths.size() && !entryPaths[selectorIndex].empty() &&
          (files[selectorIndex].empty() || files[selectorIndex].back() != '/') &&
          (!isLibraryDashboard() || selectorIndex >= LIBRARY_DASHBOARD_SHORTCUT_COUNT);
      if (selectedBook) {
        infoText = getEntryTitle(static_cast<int>(selectorIndex));
        const std::string author = getEntrySubtitle(static_cast<int>(selectorIndex));
        const std::string progress = getLibraryStateLabel(static_cast<int>(selectorIndex));
        if (!author.empty()) {
          infoText += " - " + author;
        }
        if (!progress.empty()) {
          infoText += " - " + progress;
        }
      }
    }

    // Left-truncate so the deepest directory or selected-book title is always visible.
    const char* pathStr = infoText.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
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
    const int textX = metrics.contentSidePadding + std::max(0, (pathMaxWidth - textW) / 2);
    renderer.drawText(SMALL_FONT_ID, textX, pathY, pathDisplay);
  }

  if (showHeaderStatus) {
    std::string statusText = renderer.truncatedText(SMALL_FONT_ID, headerStatusText.c_str(),
                                                    pageWidth - metrics.contentSidePadding * 2,
                                                    EpdFontFamily::BOLD);
    const int statusWidth = renderer.getTextWidth(SMALL_FONT_ID, statusText.c_str(), EpdFontFamily::BOLD);
    const int x = metrics.contentSidePadding + std::max(0, (pageWidth - metrics.contentSidePadding * 2 - statusWidth) / 2);
    const int y = metrics.topPadding + metrics.headerHeight + 1;
    renderer.drawText(SMALL_FONT_ID, x, y, statusText.c_str(), true, EpdFontFamily::BOLD);
    if (libraryIndexingActive && !librarySkippedFolderName.empty() && !librarySkippedFolderShown) {
      librarySkippedFolderShown = true;
    }
  }

  const auto labels =
      mappedInput.mapLabels((basepath == "/" && !isLibraryShelf() && libraryView != LIBRARY_VIEW_FILES) ? tr(STR_HOME) : tr(STR_BACK), files.empty() ? "" : tr(STR_OPEN),
                            files.empty() ? "" : tr(STR_DIR_UP), files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (holdPreviewVisible) {
    drawHoldPreview(renderer, tr(STR_BOOK_ACTIONS));
  } else if (sortPreviewVisible) {
    drawHoldPreview(renderer, tr(STR_SORT_VIEW));
  }

  libraryFirstRenderDone = true;
  renderer.displayBuffer();
  lastLibraryRenderFinishedMs = millis();
  libraryRendering = false;
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
