#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "./FileBrowserActivity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;
struct ShortcutDefinition;

struct HomeShortcutEntry {
  const ShortcutDefinition* definition = nullptr;
  bool isAppsHub = false;
  std::string title;
  std::string subtitle;
  int icon = 0;
  bool accessory = false;
};

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool confirmLongPressHandled = false;
  bool holdPreviewVisible = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  int coverBufferSelectionState = -99;
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  std::vector<RecentBook> recentBooks;
  std::vector<HomeShortcutEntry> homeShortcutEntries;
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onAppsOpen();
  void onReadingStatsOpen();
  void onSyncDayOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  void rebuildHomeShortcutEntries();
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);
  bool needsRecentCoverLoad(int coverHeight) const;
  void requestRemoveRecentBook(int recentIndex);
  int getRecentBookLoadCount() const;
  int getDashboardHeight() const;
  int getDashboardSelectionState() const;
  void drawDashboardSelectionOverlay(const Rect& rect);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
