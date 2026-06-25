#pragma once

#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"

namespace CompactHudRenderer {

struct Row {
  std::string label;
  std::string value;
  bool disabled = false;
};

struct ActionListConfig {
  std::string title;
  std::vector<std::string> context;
  std::vector<Row> rows;
  int selectedIndex = 0;
  int minWidth = 300;
  int maxRows = 8;
  bool drawHints = true;
  bool wrapRows = false;
};

struct ConfirmationConfig {
  std::string title;
  std::vector<std::string> body;
  std::string cancelLabel;
  std::string confirmLabel;
  int selectedAction = 1;
};

void drawActionList(GfxRenderer& renderer, MappedInputManager& mappedInput, const ActionListConfig& config);
void drawConfirmation(GfxRenderer& renderer, MappedInputManager& mappedInput, const ConfirmationConfig& config);

}  // namespace CompactHudRenderer
