#pragma once
#include "../Activity.h"

class CrashActivity final : public Activity {
  std::string panicMessage;
  std::string crashReason;
  std::string crashLocation;
  std::string libraryBreadcrumb;

 public:
  explicit CrashActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Crash", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
