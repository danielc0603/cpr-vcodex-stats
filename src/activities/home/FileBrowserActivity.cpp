#include "FileBrowserActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
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
constexpr int CARD_FOCUS_INSET = 4;
constexpr int SHELF_COVER_WIDTH = RecentBooksGrid::kCoverWidth;
constexpr int SHELF_COVER_HEIGHT = RecentBooksGrid::kCoverHeight;
constexpr uint8_t MEANINGFUL_PROGRESS_PERCENT = 2;
constexpr uint8_t LIBRARY_VIEW_DASHBOARD = 1;
constexpr uint8_t LIBRARY_VIEW_CONTINUE = 2;
constexpr uint8_t LIBRARY_VIEW_TO_READ = 3;
constexpr uint8_t LIBRARY_VIEW_FINISHED = 5;
constexpr uint8_t LIBRARY_VIEW_FILES = 7;
constexpr size_t MAX_LIBRARY_DASHBOARD_BOOKS = 240;
constexpr int MAX_LIBRARY_SCAN_DEPTH = 8;
constexpr int LIBRARY_DASHBOARD_SHORTCUT_COUNT = 2;

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
  SORT_VIEW_TITLE = 0,
  SORT_VIEW_AUTHOR = 1,
  SORT_VIEW_RECENT = 2,
  SORT_VIEW_PROGRESS = 3,
  SORT_VIEW_FILE_TYPE = 4,
  SORT_VIEW_PATH = 5,
  SORT_VIEW_BROWSE_FILES = 10,
  SORT_VIEW_COLUMNS_2 = 20,
  SORT_VIEW_COLUMNS_3 = 21,
};

std::string savedBrowserBasepath = "/";
uint8_t savedBrowserLibraryView = 0;
size_t savedBrowserSelectorIndex = 0;
bool hasSavedBrowserCursor = false;

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

std::string lowercaseCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string titleFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
  return getFileName(filename);
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
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 5, true);
  if (imageFile) {
    renderer.drawIcon(Image24Icon, rect.x + (rect.width - 24) / 2, rect.y + std::max(6, (rect.height - 24) / 2), 24,
                      24);
    return;
  }

  const int bookW = std::min(rect.width - 12, std::max(28, rect.width * 2 / 3));
  const int bookH = std::min(rect.height - 10, std::max(34, rect.height - 12));
  const int bookX = rect.x + (rect.width - bookW) / 2;
  const int bookY = rect.y + (rect.height - bookH) / 2;
  renderer.fillRect(bookX + 3, bookY + 2, std::max(1, bookW - 6), 2, true);
  renderer.drawRoundedRect(bookX, bookY, bookW, bookH, 2, 4, true);
  renderer.drawLine(bookX + 7, bookY + 3, bookX + 7, bookY + bookH - 4, true);
  renderer.drawLine(bookX + 10, bookY + 4, bookX + 10, bookY + bookH - 5, true);
  renderer.drawLine(bookX + 14, bookY + 11, bookX + bookW - 8, bookY + 11, 2, true);
  renderer.drawLine(bookX + 14, bookY + 20, bookX + bookW - 11, bookY + 20, true);
  renderer.drawLine(bookX + 14, bookY + 28, bookX + bookW - 14, bookY + 28, true);
  renderer.drawLine(bookX + bookW - 5, bookY + 6, bookX + bookW - 5, bookY + bookH - 7, true);
  renderer.drawLine(bookX + 12, bookY + bookH - 9, bookX + bookW - 11, bookY + bookH - 9, true);
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
  const int availableHeight = std::max(64, statusStripTop - inner.y - titleReserve - subtitleReserve - 12);
  const int maxCoverHeight = std::max(64, std::min(SHELF_COVER_HEIGHT, availableHeight));
  const int maxCoverWidth = std::max(54, inner.width - 8);

  int coverHeight = maxCoverHeight;
  int coverWidth = coverHeight * SHELF_COVER_WIDTH / SHELF_COVER_HEIGHT;
  if (coverWidth > maxCoverWidth) {
    coverWidth = maxCoverWidth;
    coverHeight = std::max(64, coverWidth * SHELF_COVER_HEIGHT / SHELF_COVER_WIDTH);
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

 public:
  explicit SortViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SortView", renderer, mappedInput) {}

  void onEnter() override {
    Activity::onEnter();
    const uint8_t sort = SETTINGS.librarySort;
    const uint8_t columns = SETTINGS.bookshelfColumns;
    const std::string sortPrefix = std::string(tr(STR_SORT_BY)) + ": ";
    items = {
        {SORT_VIEW_TITLE, sortPrefix + tr(STR_TITLE), sort == CrossPointSettings::LIBRARY_SORT_TITLE},
        {SORT_VIEW_AUTHOR, sortPrefix + tr(STR_AUTHOR), sort == CrossPointSettings::LIBRARY_SORT_AUTHOR},
        {SORT_VIEW_RECENT, sortPrefix + tr(STR_RECENT_BOOKS), sort == CrossPointSettings::LIBRARY_SORT_RECENT},
        {SORT_VIEW_PROGRESS, sortPrefix + tr(STR_PROGRESS), sort == CrossPointSettings::LIBRARY_SORT_PROGRESS},
        {SORT_VIEW_FILE_TYPE, sortPrefix + tr(STR_IMAGES), sort == CrossPointSettings::LIBRARY_SORT_FILE_TYPE},
        {SORT_VIEW_PATH, sortPrefix + tr(STR_BROWSE_FILES), sort == CrossPointSettings::LIBRARY_SORT_PATH},
        {SORT_VIEW_COLUMNS_2, std::string(tr(STR_FILE_VIEW_BOOKSHELF)) + ": " + tr(STR_NUM_2),
         columns == CrossPointSettings::BOOKSHELF_COLUMNS_2},
        {SORT_VIEW_COLUMNS_3, std::string(tr(STR_FILE_VIEW_BOOKSHELF)) + ": " + tr(STR_NUM_3),
         columns == CrossPointSettings::BOOKSHELF_COLUMNS_3},
        {SORT_VIEW_BROWSE_FILES, tr(STR_BROWSE_FILES), false},
    };
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
  libraryIndexingActive = false;

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

void FileBrowserActivity::clampSelector() {
  if (files.empty()) {
    selectorIndex = 0;
  } else if (selectorIndex >= files.size()) {
    selectorIndex = files.size() - 1;
  }
}

void FileBrowserActivity::loadLibraryDashboard() {
  struct ShelfInfo {
    StrId titleId;
    StrId subtitleId;
    uint8_t view;
  };
  const ShelfInfo shelves[] = {
      {StrId::STR_TO_READ, StrId::STR_BOOKS_QUEUED, LIBRARY_VIEW_TO_READ},
      {StrId::STR_FINISHED_BOOKS, StrId::STR_COMPLETED_READING, LIBRARY_VIEW_FINISHED},
  };

  for (const auto& shelf : shelves) {
    const char* shelfTitle = I18N.get(shelf.titleId);
    files.emplace_back(std::string("@") + shelfTitle);
    completedFileStates.push_back(0);
    progressFileStates.push_back(0);
    libraryFileStates.push_back(shelf.view);
    folderItemCounts.push_back(0);
    entryPaths.emplace_back();
    entryTitles.emplace_back(shelfTitle);
    entrySubtitles.emplace_back(I18N.get(shelf.subtitleId));
    addEntryCoverPlaceholder();
  }

  if (isBookshelfMode()) {
    startLibraryIndexing();
    return;
  }

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

void FileBrowserActivity::startLibraryIndexing() {
  libraryScanFolders.clear();
  libraryScanFolders.push_back("/");
  libraryIndexingActive = true;
}

void FileBrowserActivity::addLibraryBookByPath(const std::string& path) {
  const auto* statsBook = READING_STATS.findBook(path);
  const auto* metadata = LIBRARY_METADATA.findBook(path);
  uint8_t state = LIBRARY_STATE_UNREAD;
  const uint8_t progress = statsBook != nullptr ? statsBook->lastProgressPercent : 0;
  if ((metadata != nullptr && metadata->finished) || (statsBook != nullptr && statsBook->completed)) {
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
  if (!isLibraryDashboard() || files.size() <= LIBRARY_DASHBOARD_SHORTCUT_COUNT) {
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
  rows.reserve(files.size() - LIBRARY_DASHBOARD_SHORTCUT_COUNT);
  for (size_t index = LIBRARY_DASHBOARD_SHORTCUT_COUNT; index < files.size(); ++index) {
    rows.push_back(Row{files[index],
                       index < completedFileStates.size() ? completedFileStates[index] : static_cast<uint8_t>(0),
                       index < progressFileStates.size() ? progressFileStates[index] : static_cast<uint8_t>(0),
                       index < libraryFileStates.size() ? libraryFileStates[index]
                                                        : static_cast<uint8_t>(LIBRARY_STATE_UNREAD),
                       index < folderItemCounts.size() ? folderItemCounts[index] : static_cast<uint16_t>(0),
                       index < entryPaths.size() ? entryPaths[index] : "",
                       index < entryTitles.size() ? entryTitles[index] : "",
                       index < entrySubtitles.size() ? entrySubtitles[index] : "",
                       index < entryCoverPaths.size() ? entryCoverPaths[index] : "",
                       index < entryCoverSourcePaths.size() ? entryCoverSourcePaths[index] : "",
                       index < entryCoverStates.size() ? entryCoverStates[index] : ENTRY_COVER_UNKNOWN});
  }

  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    const auto* statsA = READING_STATS.findBook(a.path);
    const auto* statsB = READING_STATS.findBook(b.path);
    const std::string titleA = !a.title.empty() ? a.title : titleFromPath(a.path);
    const std::string titleB = !b.title.empty() ? b.title : titleFromPath(b.path);
    switch (SETTINGS.librarySort) {
      case CrossPointSettings::LIBRARY_SORT_AUTHOR:
        if (lowercaseCopy(a.subtitle) != lowercaseCopy(b.subtitle)) return lowercaseCopy(a.subtitle) < lowercaseCopy(b.subtitle);
        return lowercaseCopy(titleA) < lowercaseCopy(titleB);
      case CrossPointSettings::LIBRARY_SORT_RECENT: {
        const uint32_t recentA = statsA != nullptr ? statsA->lastReadAt : 0;
        const uint32_t recentB = statsB != nullptr ? statsB->lastReadAt : 0;
        if (recentA != recentB) return recentA > recentB;
        return lowercaseCopy(titleA) < lowercaseCopy(titleB);
      }
      case CrossPointSettings::LIBRARY_SORT_PROGRESS:
        if (a.progress != b.progress) return a.progress > b.progress;
        return lowercaseCopy(titleA) < lowercaseCopy(titleB);
      case CrossPointSettings::LIBRARY_SORT_FILE_TYPE: {
        const std::string extA = lowercaseCopy(getFileExtension(a.path));
        const std::string extB = lowercaseCopy(getFileExtension(b.path));
        if (extA != extB) return extA < extB;
        return lowercaseCopy(titleA) < lowercaseCopy(titleB);
      }
      case CrossPointSettings::LIBRARY_SORT_PATH:
        return lowercaseCopy(a.path) < lowercaseCopy(b.path);
      case CrossPointSettings::LIBRARY_SORT_TITLE:
      default:
        return lowercaseCopy(titleA) < lowercaseCopy(titleB);
    }
  });

  for (size_t offset = 0; offset < rows.size(); ++offset) {
    const size_t index = LIBRARY_DASHBOARD_SHORTCUT_COUNT + offset;
    files[index] = std::move(rows[offset].file);
    completedFileStates[index] = rows[offset].completed;
    progressFileStates[index] = rows[offset].progress;
    libraryFileStates[index] = rows[offset].state;
    folderItemCounts[index] = rows[offset].folderCount;
    entryPaths[index] = std::move(rows[offset].path);
    entryTitles[index] = std::move(rows[offset].title);
    entrySubtitles[index] = std::move(rows[offset].subtitle);
    entryCoverPaths[index] = std::move(rows[offset].coverPath);
    entryCoverSourcePaths[index] = std::move(rows[offset].coverSourcePath);
    entryCoverStates[index] = rows[offset].coverState;
  }
}

bool FileBrowserActivity::processLibraryIndexJob() {
  if (!libraryIndexingActive || !isBookshelfMode() || !isLibraryDashboard() || mappedInput.isAnyMappedButtonPressed()) {
    return false;
  }
  if (libraryScanFolders.empty() || files.size() >= MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT) {
    libraryIndexingActive = false;
    return false;
  }

  const std::string folderPath = libraryScanFolders.back();
  libraryScanFolders.pop_back();
  const int depth = static_cast<int>(std::count(folderPath.begin(), folderPath.end(), '/'));
  if (depth > MAX_LIBRARY_SCAN_DEPTH) {
    return false;
  }

  auto dir = Storage.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  bool addedBook = false;
  char name[500];
  for (auto file = dir.openNextFile(); file && files.size() < MAX_LIBRARY_DASHBOARD_BOOKS + LIBRARY_DASHBOARD_SHORTCUT_COUNT;
       file = dir.openNextFile()) {
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
      libraryScanFolders.push_back(childPath);
      continue;
    }

    std::string_view filename{name};
    if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
        FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
      const size_t before = files.size();
      addLibraryBookByPath(childPath);
      addedBook = addedBook || files.size() != before;
    }
    file.close();
  }
  dir.close();
  if (addedBook) {
    sortLibraryDashboardBooks();
    clampSelector();
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
  if (state != LIBRARY_STATE_FINISHED) {
    resolveEntryCover(static_cast<int>(entryCoverPaths.size()) - 1, false);
  }
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
    const bool activeRemoved = metadata != nullptr && metadata->activeRemoved;
    const bool toRead = metadata != nullptr && metadata->toRead &&
                        book.lastProgressPercent < MEANINGFUL_PROGRESS_PERCENT;
    uint8_t state = (book.completed || metadataFinished)
                        ? LIBRARY_STATE_FINISHED
                        : (book.lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT ? LIBRARY_STATE_READING : LIBRARY_STATE_UNREAD);

    if (toRead) state = LIBRARY_STATE_TO_READ;

    const bool include =
        (shelf == LIBRARY_VIEW_CONTINUE && !book.completed && !metadataFinished && !toRead && !activeRemoved &&
         book.lastProgressPercent >= MEANINGFUL_PROGRESS_PERCENT) ||
        (shelf == LIBRARY_VIEW_FINISHED && (book.completed || metadataFinished)) ||
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
  if (index < 0 || index >= static_cast<int>(files.size()) || index >= static_cast<int>(entryPaths.size())) {
    return false;
  }
  const std::string& entry = files[index];
  if (entry.empty() || entry.back() == '/') {
    return false;
  }
  if (!isLibraryShelf() && basepath == "/" && !(isLibraryDashboard() && index >= 3)) {
    return false;
  }

  const std::string& path = entryPaths[index];
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

bool FileBrowserActivity::resolveEntryCover(const int index, const bool allowGeneration) {
  if (!entryCanResolveCover(index) || index >= static_cast<int>(entryCoverPaths.size()) ||
      index >= static_cast<int>(entryCoverSourcePaths.size()) || index >= static_cast<int>(entryCoverStates.size())) {
    return false;
  }
  if (entryCoverStates[index] == ENTRY_COVER_READY || entryCoverStates[index] == ENTRY_COVER_MISSING) {
    return false;
  }

  const std::string& path = entryPaths[index];
  std::string sourceCoverPath = entryCoverSourcePaths[index];
  if (sourceCoverPath.empty()) {
    if (FsHelpers::hasEpubExtension(path)) {
      sourceCoverPath = Epub(path, "/.crosspoint").getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(path)) {
      sourceCoverPath = Xtc(path, "/.crosspoint").getThumbBmpPath();
    }
  }

  if (auto* cached = findThumbnailCacheRecord(path, sourceCoverPath, SHELF_COVER_WIDTH, SHELF_COVER_HEIGHT)) {
    if (cached->state == ENTRY_COVER_READY && !cached->thumbPath.empty() && Storage.exists(cached->thumbPath.c_str())) {
      entryCoverStates[index] = ENTRY_COVER_READY;
      entryCoverPaths[index] = cached->thumbPath;
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
      UITheme::getCoverThumbPath(sourceCoverPath, SHELF_COVER_WIDTH, SHELF_COVER_HEIGHT);
  if (!existingThumbPath.empty() && Storage.exists(existingThumbPath.c_str())) {
    entryCoverPaths[index] = existingThumbPath;
    entryCoverSourcePaths[index] = sourceCoverPath;
    entryCoverStates[index] = ENTRY_COVER_READY;
    rememberThumbnailCacheRecord(path, sourceCoverPath, existingThumbPath, SHELF_COVER_WIDTH, SHELF_COVER_HEIGHT,
                                 ENTRY_COVER_READY);
    return true;
  }

  if (!allowGeneration || (index < static_cast<int>(libraryFileStates.size()) &&
                           libraryFileStates[index] == LIBRARY_STATE_FINISHED)) {
    return false;
  }

  const std::string generatedThumbPath =
      UITheme::ensureBookCoverThumbPath(path, sourceCoverPath, SHELF_COVER_WIDTH, SHELF_COVER_HEIGHT);

  if (!generatedThumbPath.empty() && Storage.exists(generatedThumbPath.c_str())) {
    entryCoverPaths[index] = generatedThumbPath;
    entryCoverSourcePaths[index] = sourceCoverPath;
    entryCoverStates[index] = ENTRY_COVER_READY;
    rememberThumbnailCacheRecord(path, sourceCoverPath, generatedThumbPath, SHELF_COVER_WIDTH, SHELF_COVER_HEIGHT,
                                 ENTRY_COVER_READY);
    return true;
  }

  entryCoverStates[index] = ENTRY_COVER_MISSING;
  rememberThumbnailCacheRecord(path, sourceCoverPath, "", SHELF_COVER_WIDTH, SHELF_COVER_HEIGHT, ENTRY_COVER_MISSING);
  return false;
}

bool FileBrowserActivity::processVisibleCoverJob(const int pageItems) {
  if (!isBookshelfMode() || pageItems <= 0 || mappedInput.isAnyMappedButtonPressed()) {
    return false;
  }
  if (millis() - lastNavigationInputMs < 350) {
    return false;
  }

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
  return false;
}

int FileBrowserActivity::countPendingCoverJobs(const int pageItems) const {
  if (!isBookshelfMode() || pageItems <= 0) {
    return 0;
  }
  const int pageStartIndex = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + pageItems);
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

  if (hasSavedBrowserCursor && basepath == "/") {
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
    if (basepath != "/" && !(isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES)) libraryView = 0;
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    root.close();
    if (basepath != "/" && !(isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES)) libraryView = 0;
    loadFiles();
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  savedBrowserBasepath = basepath;
  savedBrowserLibraryView = libraryView;
  savedBrowserSelectorIndex = selectorIndex;
  hasSavedBrowserCursor = true;
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
  }

  if (isBookshelfMode() && basepath == "/" && isLibraryDashboard() && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      !backLongPressHandled && mappedInput.getHeldTime() >= GO_HOME_MS && !lockLongPressBack) {
    backLongPressHandled = true;
    mappedInput.consumeActiveHoldUntilRelease();
    openSortViewMenu();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backLongPressHandled) {
    backLongPressHandled = false;
    return;
  }

  // Long press BACK (1s+) goes to root folder
  // but Long press BACK (1s+) from ReaderActivity sends us here with the MappedInput already set.
  // So ignore it the first time.
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/" && !lockLongPressBack) {
    lastNavigationInputMs = millis();
    basepath = "/";
    libraryView = isBookshelfMode() ? LIBRARY_VIEW_DASHBOARD : 0;
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
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
  const bool selectedSupportsBookActions =
      hasSelection && isBookshelfMode() && !selectedIsDirectory &&
      (!isLibraryDashboard() || selectorIndex >= LIBRARY_DASHBOARD_SHORTCUT_COUNT);

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
      if (targetView == LIBRARY_VIEW_FILES) {
        libraryView = LIBRARY_VIEW_FILES;
      } else if (targetView == LIBRARY_VIEW_TO_READ || targetView == LIBRARY_VIEW_FINISHED) {
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
      if (!(isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES)) libraryView = 0;
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
      if (basepath != "/") {
        const std::string oldPath = basepath;

        if (isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES) {
          basepath = "/";
          libraryView = LIBRARY_VIEW_DASHBOARD;
          selectorIndex = 0;
          loadFiles();
          clampSelector();
          requestUpdate();
          return;
        }

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        if (basepath == "/" && !(isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES)) libraryView = 0;
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else {
        if (isBookshelfMode() && libraryView == LIBRARY_VIEW_FILES) {
          libraryView = LIBRARY_VIEW_DASHBOARD;
          selectorIndex = 0;
          loadFiles();
          clampSelector();
          requestUpdate();
          return;
        }
        if (isBookshelfMode() && libraryView != LIBRARY_VIEW_DASHBOARD) {
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
  if (isBookshelfMode()) {
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { moveBookshelfHorizontal(1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { moveBookshelfHorizontal(-1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { moveBookshelfVertical(1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] { moveBookshelfVertical(-1); });
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
    const int action = std::get<MenuResult>(result.data).action;
    switch (static_cast<SortViewAction>(action)) {
      case SORT_VIEW_TITLE:
        SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_TITLE;
        break;
      case SORT_VIEW_AUTHOR:
        SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_AUTHOR;
        break;
      case SORT_VIEW_RECENT:
        SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_RECENT;
        break;
      case SORT_VIEW_PROGRESS:
        SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_PROGRESS;
        break;
      case SORT_VIEW_FILE_TYPE:
        SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_FILE_TYPE;
        break;
      case SORT_VIEW_PATH:
        SETTINGS.librarySort = CrossPointSettings::LIBRARY_SORT_PATH;
        break;
      case SORT_VIEW_COLUMNS_2:
        SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_COLUMNS_2;
        break;
      case SORT_VIEW_COLUMNS_3:
        SETTINGS.bookshelfColumns = CrossPointSettings::BOOKSHELF_COLUMNS_3;
        break;
      case SORT_VIEW_BROWSE_FILES:
        libraryView = LIBRARY_VIEW_FILES;
        selectorIndex = 0;
        loadFiles();
        requestUpdate(true);
        return;
    }
    SETTINGS.saveToFile();
    const std::string selectedPath =
        selectorIndex < entryPaths.size() && !entryPaths[selectorIndex].empty() ? entryPaths[selectorIndex] : "";
    loadFiles();
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
      loadFiles();
      requestUpdate(true);
      return;
    case BOOK_ACTION_MARK_FINISHED:
      if (LIBRARY_METADATA.isFinished(path)) {
        LIBRARY_METADATA.removeFinishedState(path);
      } else {
        LIBRARY_METADATA.setFinished(path);
      }
      loadFiles();
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
  return SETTINGS.bookshelfColumns == CrossPointSettings::BOOKSHELF_COLUMNS_2 ? 2 : 3;
}

int FileBrowserActivity::getBookshelfCardHeight() const {
  if (isLibraryDashboard()) return getBookshelfColumns() == 2 ? 246 : 214;
  if (isLibraryShelf()) return getBookshelfColumns() == 2 ? 252 : 236;
  if (basepath != "/") return getBookshelfColumns() == 2 ? 252 : 236;
  return getBookshelfColumns() == 2 ? 246 : 214;
}

int FileBrowserActivity::getPageItems(const int contentHeight) const {
  if (isBookshelfMode()) {
    if (isLibraryDashboard()) {
      const int shelfHeight = 82;
      const int rowStride = getBookshelfCardHeight() + BOOKSHELF_CARD_GAP;
      const int listHeight = std::max(0, contentHeight - shelfHeight - BOOKSHELF_CARD_GAP * 2);
      const int rows = std::max(1, (listHeight + BOOKSHELF_CARD_GAP) / rowStride);
      return LIBRARY_DASHBOARD_SHORTCUT_COUNT + std::max(1, rows * getBookshelfColumns());
    }
    const int rowStride = getBookshelfCardHeight() + BOOKSHELF_CARD_GAP;
    const int rows = std::max(1, (contentHeight + BOOKSHELF_CARD_GAP) / rowStride);
    if (isLibraryShelf()) return std::min(6, rows * getBookshelfColumns());
    if (basepath != "/") return std::min(9, rows * getBookshelfColumns());
    return std::min(12, rows * getBookshelfColumns());
  }
  const int reserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  return UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, reserved);
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
  const int columns = getBookshelfColumns();
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

void FileBrowserActivity::renderBookshelf(const Rect& rect, const int pageItems) {
  if (isLibraryDashboard()) {
    renderLibraryDashboard(rect, pageItems);
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int columns = getBookshelfColumns();
  const int cardHeight = getBookshelfCardHeight();
  const int cardGap = BOOKSHELF_CARD_GAP;
  const int cardWidth = (rect.width - sidePadding * 2 - cardGap * (columns - 1)) / columns;
  const int pageStartIndex = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + pageItems);

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
    const Rect inner = insetRect(card, CARD_PAD);
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
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePadding = metrics.contentSidePadding;
  const int shortcutColumns = 2;
  const int bookColumns = getBookshelfColumns();
  const int cardGap = BOOKSHELF_CARD_GAP;
  const int shelfHeight = 82;
  const int shortcutCardWidth = (rect.width - sidePadding * 2 - cardGap) / shortcutColumns;
  const int bookCardHeight = getBookshelfCardHeight();
  const int bookCardWidth = (rect.width - sidePadding * 2 - cardGap * (bookColumns - 1)) / bookColumns;
  const int pageStartIndex = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
  const int pageEndIndex = std::min(static_cast<int>(files.size()), pageStartIndex + pageItems);

  for (int index = pageStartIndex; index < pageEndIndex; ++index) {
    const int localIndex = index - pageStartIndex;
    const bool selected = selectorIndex == static_cast<size_t>(index);
    const bool isShelf = index >= 0 && index < static_cast<int>(libraryFileStates.size()) &&
                         (libraryFileStates[index] == LIBRARY_VIEW_TO_READ ||
                          libraryFileStates[index] == LIBRARY_VIEW_FINISHED);

    if (isShelf && pageStartIndex == 0) {
      const int column = localIndex % shortcutColumns;
      const Rect card{rect.x + sidePadding + column * (shortcutCardWidth + cardGap), rect.y, shortcutCardWidth,
                      shelfHeight};
      drawContainedCard(renderer, card, selected, 5);

      const Rect inner = insetRect(card, 12);
      const int iconSize = 22;
      renderer.drawIcon(BookIcon, inner.x, inner.y + 4, iconSize, iconSize);
      const int textX = inner.x + iconSize + 8;
      const int textWidth = std::max(0, inner.x + inner.width - textX);
      const auto titleLines =
          renderer.wrappedText(UI_10_FONT_ID, getEntryTitle(index).c_str(), textWidth, 2, EpdFontFamily::BOLD);
      int y = inner.y + 2;
      for (const auto& line : titleLines) {
        renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str(), true, EpdFontFamily::BOLD);
        y += renderer.getLineHeight(UI_10_FONT_ID);
      }
      const auto subtitleLines = renderer.wrappedText(SMALL_FONT_ID, getEntrySubtitle(index).c_str(), inner.width, 2);
      y = std::max(y + 4, inner.y + 36);
      for (const auto& line : subtitleLines) {
        if (y > inner.y + inner.height - renderer.getLineHeight(SMALL_FONT_ID)) break;
        renderer.drawText(SMALL_FONT_ID, inner.x, y, line.c_str(), true);
        y += renderer.getLineHeight(SMALL_FONT_ID);
      }
      continue;
    }

    const int gridIndex = pageStartIndex == 0 ? localIndex - LIBRARY_DASHBOARD_SHORTCUT_COUNT : localIndex;
    if (gridIndex < 0) {
      continue;
    }
    const int column = gridIndex % bookColumns;
    const int row = gridIndex / bookColumns;
    const int gridTop = pageStartIndex == 0 ? rect.y + shelfHeight + cardGap * 2 : rect.y;
    const Rect card{rect.x + sidePadding + column * (bookCardWidth + cardGap),
                    gridTop + row * (bookCardHeight + cardGap), bookCardWidth, bookCardHeight};
    const Rect inner = insetRect(card, CARD_PAD);
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
    }
  }
}

void FileBrowserActivity::renderPageIndicator(const Rect& rect, const int pageItems) const {
  if (!isBookshelfMode() || files.empty() || pageItems <= 0 || static_cast<int>(files.size()) <= pageItems) {
    return;
  }
  const int pageCount = (static_cast<int>(files.size()) + pageItems - 1) / pageItems;
  const int currentPage = std::min(pageCount, static_cast<int>(selectorIndex) / pageItems + 1);
  const std::string pageLabel = std::to_string(currentPage) + "/" + std::to_string(pageCount);
  const int textW = renderer.getTextWidth(SMALL_FONT_ID, pageLabel.c_str());
  const int x = rect.x + rect.width - UITheme::getInstance().getMetrics().contentSidePadding - textW;
  const int y = rect.y + rect.height - renderer.getLineHeight(SMALL_FONT_ID) - 2;
  renderer.drawText(SMALL_FONT_ID, x, y, pageLabel.c_str(), true, EpdFontFamily::BOLD);
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  if (isBookshelfMode() && basepath == "/") {
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
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  if (files.empty()) {
    std::string emptyTitle = isBookshelfMode() ? tr(STR_NO_BOOKS_HERE) : tr(STR_NO_FILES_FOUND);
    std::string emptySubtitle = tr(STR_ADD_EPUB_FILES);
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
    if (isBookshelfMode()) {
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
  } else if (isBookshelfMode()) {
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
    std::string infoText = basepath;
    if (isBookshelfMode() && !files.empty() && selectorIndex < files.size()) {
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
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (SETTINGS.advancedStatusHeader && isBookshelfMode()) {
    const int pageItems = getPageItems(contentHeight);
    const int pendingCovers = countPendingCoverJobs(pageItems);
    if (libraryIndexingActive || pendingCovers > 0) {
      const std::string statusText = libraryIndexingActive
                                         ? std::string(tr(STR_INDEXING)) + " " + std::to_string(libraryScanFolders.size())
                                         : std::string(tr(STR_LOADING)) + " " + std::to_string(pendingCovers);
      const int statusWidth = renderer.getTextWidth(SMALL_FONT_ID, statusText.c_str(), EpdFontFamily::BOLD);
      const int x = std::max(metrics.contentSidePadding, pageWidth - metrics.contentSidePadding - statusWidth);
      const int y = metrics.topPadding + metrics.headerHeight - renderer.getLineHeight(SMALL_FONT_ID) - 2;
      renderer.drawText(SMALL_FONT_ID, x, y, statusText.c_str(), true, EpdFontFamily::BOLD);
    }
  }

  const auto labels =
      mappedInput.mapLabels((basepath == "/" && !isLibraryShelf() && libraryView != LIBRARY_VIEW_FILES) ? tr(STR_HOME) : tr(STR_BACK), files.empty() ? "" : tr(STR_OPEN),
                            files.empty() ? "" : tr(STR_DIR_UP), files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (holdPreviewVisible) {
    drawHoldPreview(renderer, tr(STR_BOOK_ACTIONS));
  }

  renderer.displayBuffer();
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
