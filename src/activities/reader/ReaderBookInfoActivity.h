#pragma once

#include <string>

#include "activities/Activity.h"

class ReaderBookInfoActivity final : public Activity {
 public:
  ReaderBookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  std::string title;
  bool waitForBackRelease = false;
};
