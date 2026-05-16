#pragma once

#include <SdCardFontRegistry.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FontSelectionActivity final : public Activity {
 public:
  enum class Mode { Select, Manage };

  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const SdCardFontRegistry* registry, Mode mode = Mode::Select);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ManagerTab { Installed, Download };
  enum class ManagerView { Home, Installed, Download };
  enum class DownloadState { NotLoaded, Loading, Ready, Downloading, Complete, Error };

  void handleSelection();
  void handleUninstall();
  void confirmUninstall();
  bool selectedFontIsCustom() const;
  void enterManagerView(ManagerView view);
  void leaveManagerView();
  void startCatalogLoad();
  void onWifiSelectionComplete(const ActivityResult& result);
  bool fetchCatalog();
  void downloadSelectedCatalogFont();
  int currentListSize() const;
  std::string currentDownloadValue(int index) const;
  static std::string formatSize(size_t bytes);

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;  // index used by valueSetter
  };

  struct CatalogFile {
    std::string name;
    size_t size = 0;
  };

  struct CatalogFamily {
    std::string name;
    std::string description;
    std::vector<CatalogFile> files;
    size_t totalSize = 0;
    bool installed = false;
  };

  const SdCardFontRegistry* registry_;
  Mode mode_;
  ManagerView managerView_ = ManagerView::Home;
  DownloadState downloadState_ = DownloadState::NotLoaded;
  ButtonNavigator buttonNavigator_;
  std::vector<FontEntry> fonts_;
  std::vector<CatalogFamily> catalog_;
  std::string catalogBaseUrl_;
  std::string statusMessage_;
  size_t downloadFileIndex_ = 0;
  size_t downloadFileCount_ = 0;
  size_t downloadProgress_ = 0;
  size_t downloadTotal_ = 0;
  int selectedIndex_ = 0;
};
