#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../../fontIds.h"
#include "../Activity.h"

class ConfirmationActivity : public Activity {
 private:
  // Input data
  std::string heading;
  std::string body;

  const int margin = 26;
  const int spacing = 10;
  const int fontId = UI_10_FONT_ID;

  std::string safeHeading;
  std::string safeBody;
  std::vector<std::string> bodyLines;
  int panelX = 0;
  int panelY = 0;
  int panelWidth = 0;
  int panelHeight = 0;
  int lineHeight = 0;
  int selectedAction = 1;  // 0 = Cancel, 1 = Confirm

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
