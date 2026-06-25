#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"
#include "library/LibraryCoverManager.h"
#include "library/LibraryState.h"
#include "library/LibraryWorkers.h"
#include "util/ButtonNavigator.h"

struct LibraryMenuItem {
  int action = 0;
  std::string label;
};

struct LibrarySkippedBook {
  std::string filename;
  std::string title;
  std::string path;
  std::string phase;
  std::string reason;
};

struct ResolvedLibraryRow {
  std::string path;
  std::string title;
  std::string author;
  std::string series;
  std::string coverPath;
  std::string searchText;
  std::string titleSortKey;
  std::string authorSortKey;
  uint32_t recentSortValue = 0;
  uint8_t progressPercent = 0;
  uint8_t state = 0;
  uint8_t type = 0;
  uint16_t originalIndex = 0;
  bool finished = false;
  bool toRead = false;
};

constexpr size_t LIBRARY_COVER_WARMUP_CAPACITY = 9;

struct LibraryCoverWarmupItem {
  int index = -1;
  int cardIndex = -1;
  char path[384] = {};
  char title[96] = {};
  char author[96] = {};
  char coverPath[384] = {};
};

class LibraryActivity final : public Activity {
 public:
  enum class LaunchMode : uint8_t { Normal, Reindex, Authors, Series, ToRead, Finished };

 private:
  // Deletion
  void clearFileMetadata(const std::string& fullPath);

  ButtonNavigator buttonNavigator;
  LibraryState libraryState;
  LibraryWorkers libraryWorkers;
  LibraryCoverManager libraryCoverManager;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  bool confirmLongPressHandled = false;
  bool backLongPressHandled = false;
  bool holdPreviewVisible = false;
  bool sortPreviewVisible = false;
  uint8_t libraryView = 0;
  bool rawFilesLaunch = false;
  bool pendingSeriesCollectionPicker = false;
  unsigned long lastNavigationInputMs = 0;

  // Files state
  std::string basepath = "/";
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
  std::vector<ResolvedLibraryRow> resolvedRows;
  std::vector<std::string> libraryScanFolders;
  std::vector<size_t> libraryScanOffsets;
  std::vector<std::string> libraryDiscoveredBookPaths;
  size_t libraryMetadataResolveIndex = 0;
  std::string libraryCurrentScanFolder;
  bool libraryIndexingActive = false;
  bool libraryFirstRenderDone = false;
  bool librarySafeMode = false;
  bool libraryRendering = false;
  bool libraryWorkPaused = false;
  bool librarySelectionOnlyRender = false;
  bool libraryProgressHudOnlyRender = false;
  bool libraryHasRetainedGridRects = false;
  bool libraryPostIndexCoverWarmup = false;
  bool libraryCoverWarmupPending = false;
  uint16_t libraryCoverWarmupTotal = 0;
  uint16_t libraryCoverWarmupDone = 0;
  size_t libraryPreviousSelectorIndex = 0;
  bool libraryScanRequested = false;
  bool libraryBreadcrumbClearedThisSession = false;
  uint8_t libraryProgressAction = 0;
  uint16_t libraryProgressStartBookCount = 0;
  uint16_t libraryProgressBookCount = 0;
  uint16_t libraryProgressNewBookCount = 0;
  uint16_t libraryProgressUpdatedBookCount = 0;
  uint16_t libraryProgressFoldersScanned = 0;
  uint16_t libraryProgressFilesScanned = 0;
  uint16_t libraryProgressSkippedFiles = 0;
  uint16_t librarySkippedBookCount = 0;
  uint8_t libraryIndexStage = 0;
  bool libraryIndexSummaryVisible = false;
  bool libraryIndexCanceled = false;
  unsigned long lastLibraryProgressHudMs = 0;
  uint8_t libraryFailureCount = 0;
  uint8_t libraryOverlayMode = 0;
  int libraryOverlayIndex = 0;
  bool libraryOverlaySettingsChanged = false;
  std::string libraryOverlayBookPath;
  std::string libraryOverlayBookTitle;
  std::string libraryOverlayBookEntry;
  unsigned long libraryEnteredAtMs = 0;
  unsigned long lastLibraryWorkMs = 0;
  unsigned long lastLibraryRenderFinishedMs = 0;
  unsigned long libraryHotEnterStartMs = 0;
  unsigned long libraryHotRowsReadyMs = 0;
  unsigned long libraryHotFirstGridDrawMs = 0;
  unsigned long libraryHotCoverWarmupStartMs = 0;
  std::string librarySkippedFolderName;
  std::string libraryLastMetadataPath;
  std::string libraryCurrentMetadataPath;
  std::string libraryCurrentMetadataTitle;
  std::string libraryCurrentMetadataAuthor;
  std::string libraryCurrentCoverTitle;
  std::vector<std::string> libraryBadPaths;
  std::vector<LibrarySkippedBook> librarySkippedBooks;
  std::vector<Rect> libraryRetainedItemRects;
  std::array<LibraryCoverWarmupItem, LIBRARY_COVER_WARMUP_CAPACITY> libraryCoverWarmupQueue;
  uint8_t libraryCoverWarmupQueueCount = 0;
  Rect libraryRetainedMetadataRect;
  std::string libraryRetainedRenderToken;
  int libraryRetainedContentHeight = 0;
  int libraryDirtyCoverIndex = -1;
  LaunchMode launchMode = LaunchMode::Normal;

  // Data loading
  void loadFiles();
  void loadFilesystemFiles();
  void loadLibraryDashboard();
  void loadLibraryShelf(uint8_t shelf);
  void loadLibraryRenderState();
  void saveLibraryRenderState() const;
  void beginLibrarySearchSession();
  void clearLibrarySearchSession(bool restorePrevious);
  void startLibraryIndexing();
  void resetLibraryDashboardState(bool clearPersistedIndex);
  void collectLibraryEpubPaths(const std::string& folderPath, std::vector<std::string>& paths, int depth) const;
  void repairStoresForLiveLibraryPaths(const std::vector<std::string>& livePaths);
  bool restoreLibraryDashboardSnapshot();
  bool restoreLibraryAuthorGroupSnapshot();
  void saveLibraryDashboardSnapshot() const;
  void saveLibraryAuthorGroupSnapshot() const;
  bool restoreLibraryDashboardIndex();
  void saveLibraryDashboardIndex() const;
  bool restoreLibraryIndexCheckpoint();
  void saveLibraryIndexCheckpoint(const char* stage) const;
  void clearLibraryIndexCheckpoint() const;
  void saveLibraryRootSignature() const;
  void rebuildResolvedLibraryRows();
  bool processLibraryIndexJob();
  bool addLibraryBookByPath(const std::string& path);
  bool isBadLibraryPath(const std::string& path) const;
  void markBadLibraryPath(const std::string& path);
  void recordSkippedLibraryBook(const std::string& path, const std::string& phase, const std::string& reason,
                                const std::string& title = "");
  void recordLibraryBreadcrumb(const char* phase, const std::string& path = "", const std::string& bookPath = "",
                               int index = -1, int count = -1) const;
  void clearLibraryBreadcrumb() const;
  bool shouldEnterLibrarySafeMode() const;
  void sortLibraryDashboardBooks();
  void addLibraryBook(const std::string& path, const std::string& title, const std::string& author,
                      const std::string& coverPath, uint8_t progress, uint8_t state, uint32_t recent = 0);
  void reserveLibraryRowCapacity(size_t capacity);
  bool isLibraryDashboard() const;
  bool isLibraryShelf() const;
  bool isLibraryAuthorView() const;
  bool isLibrarySeriesView() const;
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
  std::string buildLibraryRetainedRenderToken(int contentHeight) const;
  void moveBookshelfHorizontal(int delta);
  void moveBookshelfVertical(int delta);
  void moveBookshelfPage(int delta, int pageItems);
  void renderBookshelf(const Rect& rect, const int pageItems);
  void renderLibraryDashboard(const Rect& rect, const int pageItems);
  void renderAuthorGroupCard(const Rect& card, int index, bool selected);
  void renderLibraryCard(const Rect& card, int index, bool selected, bool allowDiskCover);
  bool renderLibrarySelectionOnly(int pageWidth, int pageHeight, int contentHeight, int pathLineHeight);
  bool renderLibraryDirtyCoverCard(int pageWidth, int pageHeight);
  void renderSelectionMarker(const Rect& rect, bool selected) const;
  void renderMetadataStrip(int pageWidth, int pageHeight, int contentHeight, int pathLineHeight);
  void renderLibraryProgressHub(int pageWidth, int pageHeight, int pageItems);
  void openSortViewMenu();
  void openCollectionsMenu();
  bool handleLibraryOverlayInput();
  void renderLibraryOverlay(int pageWidth, int pageHeight);
  void closeLibraryOverlay(bool redrawContent);
  std::vector<LibraryMenuItem> getLibraryOverlayItems() const;
  void executeLibraryMenuAction(int action);
  void executeLibraryCollectionsAction(int action);
  std::vector<std::string> getLibrarySeriesNames();
  void openSeriesPicker(const std::string& path, const std::string& title);
  void openSeriesCollectionPicker();
  void assignBookToSeries(const std::string& path, const std::string& seriesName);
  void addEntryCoverPlaceholder();
  bool entryCanResolveCover(int index) const;
  bool resolveEntryCover(int index, bool allowGeneration);
  bool resolveEntryCoverWithWatchdog(int index, bool allowGeneration);
  bool isCoverWorkCancelled() const;
  static bool coverWorkCancelledThunk(void* context);
  bool processVisibleCoverJob(int pageItems);
  int countPendingCoverJobs(int pageItems) const;
  int countCoverWindowCandidates(int pageItems) const;
  void buildCoverWarmupQueue(int pageItems);
  void beginPostIndexCoverWarmup(int pageItems);
  std::vector<LibraryCoverWindowEntry> collectCoverWindowEntries(int pageItems, bool firstPage) const;
  void updateActiveCoverWindow(int pageItems);
  void openBookActions(size_t index);
  void handleBookAction(int action, const std::string& path, const std::string& title, const std::string& entry);
  void confirmDeleteFile(const std::string& fullPath, const std::string& label);

 public:
  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           LaunchMode launchMode = LaunchMode::Normal)
      : Activity("Library", renderer, mappedInput),
        launchMode(launchMode),
        rawFilesLaunch(false),
        basepath("/") {}
  static void clearGeneratedLibraryCache();
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
};
