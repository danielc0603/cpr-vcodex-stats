#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

struct Rect;

class FileBrowserActivity final : public Activity {
 private:
  // Deletion
  void clearFileMetadata(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  bool confirmLongPressHandled = false;
  bool backLongPressHandled = false;
  bool holdPreviewVisible = false;
  uint8_t libraryView = 0;
  bool rawFilesLaunch = false;
  unsigned long lastNavigationInputMs = 0;

  // Files state
  std::string basepath = "/";
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
  std::vector<std::string> libraryScanFolders;
  std::vector<size_t> libraryScanOffsets;
  std::string libraryCurrentScanFolder;
  bool libraryIndexingActive = false;
  bool libraryFirstRenderDone = false;
  bool librarySafeMode = false;
  bool libraryRendering = false;
  bool libraryWorkPaused = false;
  bool libraryScanRequested = false;
  bool librarySkippedFolderShown = false;
  bool libraryBreadcrumbClearedThisSession = false;
  uint8_t libraryFailureCount = 0;
  unsigned long libraryEnteredAtMs = 0;
  unsigned long lastLibraryWorkMs = 0;
  unsigned long lastLibraryRenderFinishedMs = 0;
  std::string librarySkippedFolderName;
  std::vector<std::string> libraryBadPaths;

  // Data loading
  void loadFiles();
  void loadFilesystemFiles();
  void loadLibraryDashboard();
  void loadLibraryShelf(uint8_t shelf);
  void startLibraryIndexing();
  bool restoreLibraryDashboardSnapshot();
  void saveLibraryDashboardSnapshot() const;
  bool restoreLibraryDashboardIndex();
  void saveLibraryDashboardIndex() const;
  bool isLibraryRootSignatureChanged() const;
  void saveLibraryRootSignature() const;
  bool processLibraryIndexJob();
  void addLibraryBookByPath(const std::string& path);
  bool isBadLibraryPath(const std::string& path) const;
  void markBadLibraryPath(const std::string& path);
  void recordLibraryBreadcrumb(const char* phase, const std::string& path = "", const std::string& bookPath = "",
                               int index = -1, int count = -1) const;
  void clearLibraryBreadcrumb() const;
  bool shouldEnterLibrarySafeMode() const;
  void sortLibraryDashboardBooks();
  void addLibraryBook(const std::string& path, const std::string& title, const std::string& author,
                      const std::string& coverPath, uint8_t progress, uint8_t state);
  bool isLibraryDashboard() const;
  bool isLibraryShelf() const;
  bool isRawBrowseFilesMode() const;
  bool usesBookshelfGrid() const;
  void clampSelector();
  size_t findEntry(const std::string& name) const;
  bool isBookshelfMode() const;
  int getBookshelfColumns() const;
  int getBookshelfRows() const;
  int getBookshelfCardHeight() const;
  int getPageItems(const int contentHeight) const;
  std::vector<int> getVisibleDashboardIndices() const;
  int getVisibleDashboardPosition(const std::vector<int>& visibleIndices) const;
  uint16_t countFolderItems(const std::string& folderName) const;
  std::string getFullPathForEntry(const std::string& entry) const;
  std::string getLibraryStateLabel(int index) const;
  std::string getEntryTitle(int index) const;
  std::string getEntrySubtitle(int index) const;
  void moveBookshelfHorizontal(int delta);
  void moveBookshelfVertical(int delta);
  void moveBookshelfPage(int delta, int pageItems);
  void renderBookshelf(const Rect& rect, const int pageItems);
  void renderLibraryDashboard(const Rect& rect, const int pageItems);
  void renderPageIndicator(const Rect& rect, int pageItems) const;
  void openSortViewMenu();
  void addEntryCoverPlaceholder();
  bool entryCanResolveCover(int index) const;
  bool resolveEntryCover(int index, bool allowGeneration);
  bool processVisibleCoverJob(int pageItems);
  int countPendingCoverJobs(int pageItems) const;
  void openBookActions(size_t index);
  void handleBookAction(int action, const std::string& path, const std::string& title, const std::string& entry);
  void confirmDeleteFile(const std::string& fullPath, const std::string& label);

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               bool rawFiles = false)
      : Activity("FileBrowser", renderer, mappedInput),
        rawFilesLaunch(rawFiles),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
