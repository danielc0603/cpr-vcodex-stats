#include "FileBrowserActivity.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
#include <utility>

#include "../util/CompactHudRenderer.h"
#include "../util/ConfirmationActivity.h"
#include "CrossPointSettings.h"
#include "LibraryMetadataStore.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long RAW_FILE_HOLD_MS = 1000;

enum RawFileAction : int {
  RAW_FILE_ACTION_OPEN = 0,
  RAW_FILE_ACTION_TOGGLE_FINISHED = 1,
  RAW_FILE_ACTION_DELETE = 2,
  RAW_FILE_ACTION_COUNT = 3,
};

std::string rawFileExtension(const std::string& filename) {
  if (!filename.empty() && filename.back() == '/') {
    return "";
  }
  const auto pos = filename.find_last_of('.');
  if (pos == std::string::npos || pos == filename.size() - 1) {
    return "";
  }
  std::string ext = filename.substr(pos + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return ext;
}

std::string rawFileName(const std::string& filename) {
  std::string name = filename;
  if (!name.empty() && name.back() == '/') {
    name.pop_back();
  }
  const auto slash = name.find_last_of('/');
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    name = name.substr(0, dot);
  }
  return name;
}

bool splitAuthorTitleFilename(const std::string& filename, std::string& author, std::string& title) {
  if (!filename.empty() && filename.back() == '/') {
    return false;
  }
  const std::string base = rawFileName(filename);
  const size_t sep = base.find(" - ");
  if (sep == std::string::npos || sep == 0 || sep + 3 >= base.size()) {
    return false;
  }
  author = base.substr(0, sep);
  title = base.substr(sep + 3);
  return !author.empty() && !title.empty();
}

std::string rawFileDisplayTitle(const std::string& filename) {
  if (!filename.empty() && filename.back() == '/') {
    return rawFileName(filename);
  }
  std::string author;
  std::string title;
  if (splitAuthorTitleFilename(filename, author, title)) {
    return title;
  }
  return rawFileName(filename);
}

std::string rawFileDisplaySubtitle(const std::string& filename) {
  std::string author;
  std::string title;
  if (splitAuthorTitleFilename(filename, author, title)) {
    return author;
  }
  return "";
}

std::string normalizeSortName(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

int readNumber(const std::string& value, size_t& index) {
  int result = 0;
  while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) {
    result = result * 10 + (value[index] - '0');
    ++index;
  }
  return result;
}

bool naturalLess(const std::string& lhs, const std::string& rhs) {
  const bool lhsDir = !lhs.empty() && lhs.back() == '/';
  const bool rhsDir = !rhs.empty() && rhs.back() == '/';
  if (lhsDir != rhsDir) {
    return lhsDir;
  }

  const std::string a = normalizeSortName(lhs);
  const std::string b = normalizeSortName(rhs);
  size_t ai = 0;
  size_t bi = 0;
  while (ai < a.size() && bi < b.size()) {
    const bool an = std::isdigit(static_cast<unsigned char>(a[ai]));
    const bool bn = std::isdigit(static_cast<unsigned char>(b[bi]));
    if (an && bn) {
      const int av = readNumber(a, ai);
      const int bv = readNumber(b, bi);
      if (av != bv) {
        return av < bv;
      }
      continue;
    }
    if (a[ai] != b[bi]) {
      return a[ai] < b[bi];
    }
    ++ai;
    ++bi;
  }
  return a.size() < b.size();
}

bool isSupportedRawEntry(const std::string_view filename) {
  return FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
         FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
         FsHelpers::hasBmpExtension(filename);
}
}  // namespace

void FileBrowserActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
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
    } else if (isSupportedRawEntry(std::string_view{name})) {
      files.emplace_back(name);
    }
    file.close();
  }
  root.close();

  std::sort(files.begin(), files.end(), naturalLess);
  clampSelector();
}

void FileBrowserActivity::clampSelector() {
  if (files.empty()) {
    selectorIndex = 0;
  } else if (selectorIndex >= files.size()) {
    selectorIndex = files.size() - 1;
  }
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); ++i) {
    if (files[i] == name) {
      return i;
    }
  }
  return 0;
}

std::string FileBrowserActivity::getFullPathForEntry(const std::string& entry) const {
  if (basepath == "/") {
    return "/" + entry;
  }
  return basepath + "/" + entry;
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();
  confirmLongPressHandled = false;
  consumeInitialConfirm = true;
  sawAllButtonsReleasedAfterEnter = false;
  requireFreshConfirmPress = true;
  mappedInput.armPressedButtonsReleaseGuard();
  mappedInput.armConfirmReleaseGuard();

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    root.close();
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    selectorIndex = findEntry(pos == std::string::npos ? oldPath : oldPath.substr(pos + 1));
  } else {
    root.close();
    loadFiles();
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  closeActionHud();
  pendingFinishedToggle = false;
  pendingFinishedPath.clear();
  pendingFinishedTitle.clear();
  pendingFinishedAuthor.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
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

std::string FileBrowserActivity::resolveBookId(const std::string& fullPath, const std::string& title,
                                               const std::string& author) const {
  if (const auto* metadata = LIBRARY_METADATA.findBook(fullPath)) {
    if (!metadata->stableId.empty()) return metadata->stableId;
    if (!metadata->bookId.empty()) return metadata->bookId;
  }
  if (const auto* stats = READING_STATS.findMatchingBookForPath(fullPath, title, author)) {
    if (!stats->bookId.empty()) return stats->bookId;
  }
  return "";
}

bool FileBrowserActivity::isBookFinished(const std::string& fullPath, const std::string& title,
                                         const std::string& author) const {
  const std::string bookId = resolveBookId(fullPath, title, author);
  if (!bookId.empty() && LIBRARY_METADATA.isFinished(bookId)) {
    return true;
  }
  if (LIBRARY_METADATA.isFinished(fullPath)) {
    return true;
  }
  const auto* stats = READING_STATS.findMatchingBookForPath(fullPath, title, author);
  return stats != nullptr && stats->completed;
}

void FileBrowserActivity::toggleFinishedState(const std::string& fullPath, const std::string& title,
                                              const std::string& author) {
  const std::string bookId = resolveBookId(fullPath, title, author);
  const std::string bookKey = !bookId.empty() ? bookId : fullPath;
  if (isBookFinished(fullPath, title, author)) {
    LIBRARY_METADATA.removeFinishedState(fullPath, bookId);
  } else {
    LIBRARY_METADATA.setFinished(fullPath, bookId);
    if (SETTINGS.removeReadBooksFromRecents) {
      RECENT_BOOKS.removeBook(bookKey);
      RECENT_BOOKS.removeBook(fullPath);
    }
  }
}

void FileBrowserActivity::openActionHud(const std::string& entry) {
  actionHudEntry = entry;
  actionHudPath = getFullPathForEntry(entry);
  actionHudTitle = rawFileDisplayTitle(entry);
  actionHudAuthor = rawFileDisplaySubtitle(entry);
  actionHudIndex = 0;
  actionHudVisible = true;
  mappedInput.armConfirmReleaseGuard();
  requestUpdate();
}

void FileBrowserActivity::closeActionHud() {
  actionHudVisible = false;
  actionHudIndex = 0;
  actionHudEntry.clear();
  actionHudPath.clear();
  actionHudTitle.clear();
  actionHudAuthor.clear();
}

void FileBrowserActivity::handleActionHudConfirm() {
  const std::string path = actionHudPath;
  const std::string entry = actionHudEntry;
  const std::string title = actionHudTitle;
  const std::string author = actionHudAuthor;
  const int action = actionHudIndex;
  closeActionHud();
  if (path.empty()) {
    requestUpdate();
    return;
  }
  if (action == RAW_FILE_ACTION_OPEN) {
    onSelectBook(path);
    return;
  }
  if (action == RAW_FILE_ACTION_TOGGLE_FINISHED) {
    pendingFinishedPath = path;
    pendingFinishedTitle = title;
    pendingFinishedAuthor = author;
    pendingFinishedToggle = true;
    requestUpdate();
    return;
  }
  if (action == RAW_FILE_ACTION_DELETE) {
    confirmDeleteFile(path, entry.empty() ? title : entry);
    return;
  }
  requestUpdate();
}

void FileBrowserActivity::confirmDeleteFile(const std::string& fullPath, const std::string& label) {
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_BOOK), label.c_str()),
      [this, fullPath](const ActivityResult& result) {
        if (result.isCancelled) {
          return;
        }

        const auto* metadata = LIBRARY_METADATA.findBook(fullPath);
        const std::string bookId = metadata != nullptr ? metadata->bookId : "";
        clearFileMetadata(fullPath);
        LIBRARY_METADATA.removeFromToRead(fullPath);
        LIBRARY_METADATA.removeFinishedState(fullPath);
        LIBRARY_METADATA.removeActiveReadingState(fullPath);
        READING_STATS.removeBook(fullPath);
        RECENT_BOOKS.removeBook(fullPath);
        if (!bookId.empty()) {
          RECENT_BOOKS.removeBook(bookId);
        }
        Storage.remove(fullPath.c_str());
        loadFiles();
        clampSelector();
        requestUpdate();
      });
}

void FileBrowserActivity::loop() {
  if (pendingFinishedToggle && !actionHudVisible) {
    pendingFinishedToggle = false;
    const std::string path = pendingFinishedPath;
    const std::string title = pendingFinishedTitle;
    const std::string author = pendingFinishedAuthor;
    pendingFinishedPath.clear();
    pendingFinishedTitle.clear();
    pendingFinishedAuthor.clear();
    if (!path.empty()) {
      toggleFinishedState(path, title, author);
    }
    requestUpdate();
    return;
  }

  if (actionHudVisible) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeActionHud();
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      handleActionHudConfirm();
      return;
    }
    buttonNavigator.onNext([this] {
      actionHudIndex = ButtonNavigator::nextIndex(actionHudIndex, RAW_FILE_ACTION_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      actionHudIndex = ButtonNavigator::previousIndex(actionHudIndex, RAW_FILE_ACTION_COUNT);
      requestUpdate();
    });
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }
  if (consumeInitialConfirm) {
    if (!sawAllButtonsReleasedAfterEnter) {
      if (mappedInput.isAnyMappedButtonPressed() || mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        mappedInput.armPressedButtonsReleaseGuard();
        mappedInput.armConfirmReleaseGuard();
        return;
      }
      sawAllButtonsReleasedAfterEnter = true;
      mappedInput.armConfirmReleaseGuard();
      return;
    }
    if (requireFreshConfirmPress && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      mappedInput.armConfirmReleaseGuard();
      return;
    }
    if (requireFreshConfirmPress) {
      if (!mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        consumeInitialConfirm = false;
      } else {
        requireFreshConfirmPress = false;
        consumeInitialConfirm = false;
      }
    } else {
      consumeInitialConfirm = false;
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

  const int listSize = static_cast<int>(files.size());
  buttonNavigator.onNextPress([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    clampSelector();
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    clampSelector();
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    clampSelector();
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    clampSelector();
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmLongPressHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (files.empty()) {
      return;
    }
    clampSelector();
    const std::string entry = files[selectorIndex];
    const bool isDirectory = !entry.empty() && entry.back() == '/';

    if (mappedInput.getHeldTime() >= RAW_FILE_HOLD_MS && !isDirectory) {
      confirmLongPressHandled = true;
      openActionHud(entry);
      return;
    }
    if (confirmLongPressHandled) {
      confirmLongPressHandled = false;
      return;
    }

    if (isDirectory) {
      basepath = getFullPathForEntry(entry.substr(0, entry.size() - 1));
      selectorIndex = 0;
      loadFiles();
      requestUpdate();
      return;
    }

    onSelectBook(getFullPathForEntry(entry));
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= RAW_FILE_HOLD_MS) {
      if (basepath != "/") {
        basepath = "/";
        selectorIndex = 0;
        loadFiles();
        requestUpdate();
      }
      return;
    }

    if (basepath != "/") {
      const std::string oldPath = basepath;
      basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
      if (basepath.empty()) {
        basepath = "/";
      }
      loadFiles();

      const auto pos = oldPath.find_last_of('/');
      selectorIndex = findEntry(oldPath.substr(pos + 1) + "/");
      requestUpdate();
    } else {
      onGoHome();
    }
  }
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  const std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : rawFileName(basepath);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;

  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND), true,
                      EpdFontFamily::BOLD);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(files.size()), selectorIndex,
        [this](int index) { return rawFileDisplayTitle(files[index]); },
        [this](int index) { return rawFileDisplaySubtitle(files[index]); },
        [this](int index) { return UITheme::getFileIcon(files[index]); },
        [this](int index) { return rawFileExtension(files[index]); }, false, nullptr);
  }

  const int pathWidth = pageWidth - metrics.contentSidePadding * 2;
  const std::string displayPath = renderer.truncatedText(SMALL_FONT_ID, basepath.c_str(), pathWidth);
  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, displayPath.c_str(), true);

  const auto labels = mappedInput.mapLabels(basepath == "/" ? tr(STR_HOME) : tr(STR_BACK),
                                            files.empty() ? "" : tr(STR_OPEN),
                                            files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (actionHudVisible) {
    const bool finished = isBookFinished(actionHudPath, actionHudTitle, actionHudAuthor);
    CompactHudRenderer::ActionListConfig config;
    config.title = actionHudTitle.empty() ? rawFileName(actionHudEntry) : actionHudTitle;
    if (!actionHudAuthor.empty()) {
      config.context = {actionHudAuthor};
    }
    config.rows = {{tr(STR_OPEN), ""},
                   {finished ? tr(STR_MARK_UNFINISHED) : tr(STR_MARK_FINISHED), ""},
                   {tr(STR_DELETE_BOOK), ""}};
    config.selectedIndex = actionHudIndex;
    config.minWidth = 330;
    config.maxRows = RAW_FILE_ACTION_COUNT;
    config.wrapRows = true;
    CompactHudRenderer::drawActionList(renderer, mappedInput, config);
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
