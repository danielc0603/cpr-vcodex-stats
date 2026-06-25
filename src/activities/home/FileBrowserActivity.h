#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  bool confirmLongPressHandled = false;
  bool lockLongPressBack = false;
  bool consumeInitialConfirm = false;
  bool sawAllButtonsReleasedAfterEnter = false;
  bool requireFreshConfirmPress = false;
  bool actionHudVisible = false;
  int actionHudIndex = 0;
  std::string actionHudEntry;
  std::string actionHudPath;
  std::string actionHudTitle;
  std::string actionHudAuthor;
  bool pendingFinishedToggle = false;
  std::string pendingFinishedPath;
  std::string pendingFinishedTitle;
  std::string pendingFinishedAuthor;
  std::string basepath = "/";
  std::vector<std::string> files;

  void loadFiles();
  void clearFileMetadata(const std::string& fullPath);
  void clampSelector();
  size_t findEntry(const std::string& name) const;
  std::string getFullPathForEntry(const std::string& entry) const;
  std::string resolveBookId(const std::string& fullPath, const std::string& title, const std::string& author) const;
  bool isBookFinished(const std::string& fullPath, const std::string& title, const std::string& author) const;
  void openActionHud(const std::string& entry);
  void closeActionHud();
  void handleActionHudConfirm();
  void toggleFinishedState(const std::string& fullPath, const std::string& title, const std::string& author);
  void confirmDeleteFile(const std::string& fullPath, const std::string& label);

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/")
      : Activity("FileBrowser", renderer, mappedInput),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
