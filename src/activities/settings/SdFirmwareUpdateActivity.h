#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

class SdFirmwareUpdateActivity final : public Activity {
 public:
  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SdFirmwareUpdate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  bool skipLoopDelay() override { return state == INSTALLING; }
  bool preventAutoSleep() override { return state == INSTALLING; }
  void render(RenderLock&&) override;

 private:
  enum State { PICK_FILE, CONFIRM_INSTALL, INSTALLING, SUCCESS, FAILED };

  State state = PICK_FILE;
  std::vector<std::string> firmwareFiles;
  int selectedIndex = 0;
  int installPercent = 0;
  std::string statusText;

  void discoverFirmwareFiles();
  void scanFolder(const char* folder);
  bool installSelectedFirmware();
  const std::string& selectedPath() const;
  void moveSelection(int delta);
};
