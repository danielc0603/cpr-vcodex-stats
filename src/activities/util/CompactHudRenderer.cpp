#include "CompactHudRenderer.h"

#include <I18n.h>

#include <algorithm>

#include "HalDisplay.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kPanelRadius = 8;
constexpr int kPanelPad = 16;
constexpr int kRowHeight = 39;
constexpr int kWrappedRowHeight = 52;
constexpr int kMinRowHeight = 25;

Rect alignedPanelClearRect(const int pageWidth, const int pageHeight, const int x, const int y, const int w,
                           const int h) {
  const int clearX = std::max(0, (x / 8) * 8);
  const int clearY = std::max(0, y);
  const int clearRight = std::min(pageWidth, ((x + w + 7) / 8) * 8);
  const int clearBottom = std::min(pageHeight, y + h);
  return Rect{clearX, clearY, std::max(0, clearRight - clearX), std::max(0, clearBottom - clearY)};
}

int centeredPanelWidth(const int pageWidth, const int minWidth) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int target = pageWidth * 68 / 100;
  return std::min(std::max(minWidth, target), pageWidth - metrics.contentSidePadding * 2);
}

void drawPanelBase(GfxRenderer& renderer, const Rect& panel, const Rect& clear) {
  (void)clear;
  renderer.fillRoundedRect(panel.x, panel.y, panel.width, panel.height, kPanelRadius, Color::White);
  renderer.drawRoundedRect(panel.x, panel.y, panel.width, panel.height, 1, kPanelRadius, true);
}

int drawTitleAndContext(GfxRenderer& renderer, const Rect& panel, const std::string& title,
                        const std::vector<std::string>& context) {
  const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const std::string safeTitle =
      renderer.truncatedText(UI_10_FONT_ID, title.c_str(), panel.width - kPanelPad * 2, EpdFontFamily::BOLD);
  const int titleW = renderer.getTextWidth(UI_10_FONT_ID, safeTitle.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, panel.x + std::max(0, (panel.width - titleW) / 2), panel.y + 13,
                    safeTitle.c_str(), true, EpdFontFamily::BOLD);
  int dividerY = panel.y + 38;
  if (!context.empty()) {
    int contextY = dividerY + 8;
    for (size_t index = 0; index < context.size() && index < 3; ++index) {
      const auto style = index == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const std::string safe =
          renderer.truncatedText(SMALL_FONT_ID, context[index].c_str(), panel.width - kPanelPad * 3, style);
      const int textW = renderer.getTextWidth(SMALL_FONT_ID, safe.c_str(), style);
      renderer.drawText(SMALL_FONT_ID, panel.x + std::max(0, (panel.width - textW) / 2), contextY, safe.c_str(),
                        true, style);
      contextY += smallLineHeight + 2;
    }
    dividerY = contextY + 4;
  }
  renderer.drawLine(panel.x + kPanelPad, dividerY, panel.x + panel.width - kPanelPad, dividerY, true);
  return dividerY + 10;
}

void drawButtonHints(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* left, const char* right) {
  const auto labels = mappedInput.mapLabels(I18N.get(StrId::STR_BACK), I18N.get(StrId::STR_SELECT), left, right);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void drawDefaultButtonHints(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  drawButtonHints(renderer, mappedInput, I18N.get(StrId::STR_DIR_LEFT), I18N.get(StrId::STR_DIR_RIGHT));
}

void presentHud(GfxRenderer& renderer) { renderer.displayBuffer(HalDisplay::FAST_REFRESH); }

std::vector<std::string> wrapTextToLines(GfxRenderer& renderer, const std::string& text, const int fontId,
                                         const int maxWidth, const EpdFontFamily::Style style, const int maxLines) {
  std::vector<std::string> lines;
  if (text.empty() || maxLines <= 0) {
    return lines;
  }

  std::string current;
  size_t start = 0;
  while (start < text.size() && static_cast<int>(lines.size()) < maxLines) {
    const size_t space = text.find(' ', start);
    const std::string word = text.substr(start, space == std::string::npos ? std::string::npos : space - start);
    const std::string candidate = current.empty() ? word : current + " " + word;
    if (!current.empty() && renderer.getTextWidth(fontId, candidate.c_str(), style) > maxWidth) {
      lines.push_back(current);
      current = word;
    } else {
      current = candidate;
    }
    if (space == std::string::npos) {
      break;
    }
    start = space + 1;
  }
  if (!current.empty() && static_cast<int>(lines.size()) < maxLines) {
    lines.push_back(current);
  }
  return lines;
}
}  // namespace

namespace CompactHudRenderer {

void drawActionList(GfxRenderer& renderer, MappedInputManager& mappedInput, const ActionListConfig& config) {
  if (config.rows.empty()) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int panelW = centeredPanelWidth(pageWidth, config.minWidth);
  const int contextLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int contextHeight =
      config.context.empty() ? 0 : std::min<int>(config.context.size(), 3) * (contextLineHeight + 2) + 8;
  const int rowCount = static_cast<int>(config.rows.size());
  const int preferredRowHeight = config.wrapRows ? kWrappedRowHeight : kRowHeight;
  const int targetVisibleRows = std::min(rowCount, std::max(1, config.maxRows));
  const int availablePanelH = pageHeight - metrics.topPadding * 2 - metrics.buttonHintsHeight - 8;
  const int naturalPanelH = 58 + contextHeight + targetVisibleRows * preferredRowHeight + 28;
  const int panelH = std::min(availablePanelH, naturalPanelH);
  const int rowHeight =
      std::max(kMinRowHeight, std::min(preferredRowHeight, (panelH - 58 - contextHeight - 28) / targetVisibleRows));
  const int visibleRows = std::min(rowCount, std::max(1, (panelH - 58 - contextHeight - 28) / rowHeight));
  const int panelX = (pageWidth - panelW) / 2;
  const int minY = metrics.topPadding + 8;
  const int maxY = pageHeight - metrics.buttonHintsHeight - panelH - 6;
  const int panelY = std::max(minY, std::min((pageHeight - panelH) / 2, maxY));
  const Rect panel{panelX, panelY, panelW, panelH};
  const Rect clear = alignedPanelClearRect(pageWidth, pageHeight, panel.x, panel.y, panel.width, panel.height);

  drawPanelBase(renderer, panel, clear);
  const int listTop = drawTitleAndContext(renderer, panel, config.title, config.context);
  const int rowX = panel.x + kPanelPad + 2;
  const int rowW = panel.width - (kPanelPad + 2) * 2;
  const int rowH = std::max(20, rowHeight - 4);
  const int selectedIndex = std::clamp(config.selectedIndex, 0, static_cast<int>(config.rows.size()) - 1);
  const int startIndex = selectedIndex >= visibleRows ? selectedIndex - visibleRows + 1 : 0;
  for (int row = 0; row < visibleRows; ++row) {
    const int index = startIndex + row;
    const auto& item = config.rows[index];
    const int rowY = listTop + row * rowHeight;
    const bool selected = index == selectedIndex;
    if (selected) {
      renderer.fillRoundedRect(rowX, rowY, rowW, rowH, 6, Color::LightGray);
      renderer.drawRoundedRect(rowX, rowY, rowW, rowH, 2, 6, true);
    }
    const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    if (config.wrapRows) {
      const int textX = rowX + 10;
      const int textW = rowW - 20;
      const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
      const int valueLineH = renderer.getLineHeight(SMALL_FONT_ID);
      const auto labelLines = wrapTextToLines(renderer, item.label, UI_10_FONT_ID, textW, style,
                                              item.value.empty() ? 2 : 1);
      const int valueW =
          item.value.empty() ? 0 : renderer.getTextWidth(SMALL_FONT_ID, item.value.c_str(), EpdFontFamily::REGULAR);
      const int labelW =
          labelLines.empty() ? 0 : renderer.getTextWidth(UI_10_FONT_ID, labelLines.front().c_str(), style);
      const bool valueFitsRight = !item.value.empty() && !labelLines.empty() && valueW < textW / 2 &&
                                  labelW + valueW + 18 <= textW;
      int textY = rowY + 5;
      for (size_t lineIndex = 0; lineIndex < labelLines.size(); ++lineIndex) {
        renderer.drawText(UI_10_FONT_ID, textX, textY, labelLines[lineIndex].c_str(), true, style);
        if (lineIndex == 0 && valueFitsRight) {
          renderer.drawText(SMALL_FONT_ID, textX + textW - valueW, textY + 1, item.value.c_str(), true);
        }
        textY += lineH + 1;
      }
      if (!item.value.empty() && !valueFitsRight) {
        const auto valueLines =
            wrapTextToLines(renderer, item.value, SMALL_FONT_ID, textW, EpdFontFamily::REGULAR, 2);
        if (!valueLines.empty()) {
          for (const auto& line : valueLines) {
            renderer.drawText(SMALL_FONT_ID, textX, textY, line.c_str(), true);
            textY += valueLineH;
          }
        }
      }
    } else {
      const int valueWidth = item.value.empty() ? 0 : std::min(rowW / 2, std::max(64, rowW * 38 / 100));
      const int labelWidth = rowW - valueWidth - 20;
      const std::string safeLabel = renderer.truncatedText(UI_10_FONT_ID, item.label.c_str(), labelWidth, style);
      const int textY = rowY + std::max(4, (rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2);
      renderer.drawText(UI_10_FONT_ID, rowX + 10, textY, safeLabel.c_str(), true, style);
      if (!item.value.empty()) {
        const std::string safeValue =
            renderer.truncatedText(UI_10_FONT_ID, item.value.c_str(), valueWidth, EpdFontFamily::REGULAR);
        const int valueW = renderer.getTextWidth(UI_10_FONT_ID, safeValue.c_str());
        renderer.drawText(UI_10_FONT_ID, rowX + rowW - valueW - 10, textY, safeValue.c_str(), true);
      }
    }
  }
  if (config.drawHints) {
    drawButtonHints(renderer, mappedInput, I18N.get(StrId::STR_DIR_UP), I18N.get(StrId::STR_DIR_DOWN));
  } else {
    drawDefaultButtonHints(renderer, mappedInput);
  }
  presentHud(renderer);
}

void drawConfirmation(GfxRenderer& renderer, MappedInputManager& mappedInput, const ConfirmationConfig& config) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int panelW = centeredPanelWidth(pageWidth, 260);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int bodyLines = std::max(1, std::min<int>(config.body.size(), 4));
  const int panelH = std::min(pageHeight - metrics.topPadding * 2 - metrics.buttonHintsHeight - 8,
                              72 + bodyLines * (lineHeight + 3) + 40);
  const int panelX = (pageWidth - panelW) / 2;
  const int panelY = std::max(metrics.topPadding, (pageHeight - metrics.buttonHintsHeight - panelH) / 2);
  const Rect panel{panelX, panelY, panelW, panelH};
  const Rect clear = alignedPanelClearRect(pageWidth, pageHeight, panel.x, panel.y, panel.width, panel.height);

  drawPanelBase(renderer, panel, clear);
  int y = drawTitleAndContext(renderer, panel, config.title, {});
  const int textX = panel.x + kPanelPad;
  const int textW = panel.width - kPanelPad * 2;
  for (int index = 0; index < bodyLines; ++index) {
    const std::string line =
        config.body.empty() ? "" : renderer.truncatedText(UI_10_FONT_ID, config.body[index].c_str(), textW);
    renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str(), true);
    y += lineHeight + 3;
  }

  const int rowY = panel.y + panel.height - 42;
  const int rowHeight = 30;
  const int halfWidth = (panel.width - kPanelPad * 2 - 8) / 2;
  const int cancelX = panel.x + kPanelPad;
  const int confirmX = cancelX + halfWidth + 8;
  if (config.selectedAction == 0) {
    renderer.fillRoundedRect(cancelX, rowY, halfWidth, rowHeight, 6, Color::LightGray);
  }
  renderer.drawRoundedRect(cancelX, rowY, halfWidth, rowHeight, config.selectedAction == 0 ? 2 : 1, 6, true);
  if (config.selectedAction == 1) {
    renderer.fillRoundedRect(confirmX, rowY, halfWidth, rowHeight, 6, Color::LightGray);
  }
  renderer.drawRoundedRect(confirmX, rowY, halfWidth, rowHeight, config.selectedAction == 1 ? 2 : 1, 6, true);

  const char* cancelLabel = config.cancelLabel.empty() ? I18N.get(StrId::STR_CANCEL) : config.cancelLabel.c_str();
  const char* confirmLabel = config.confirmLabel.empty() ? I18N.get(StrId::STR_CONFIRM) : config.confirmLabel.c_str();
  renderer.drawText(UI_10_FONT_ID, cancelX + 9, rowY + 7, cancelLabel, true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, confirmX + 9, rowY + 7, confirmLabel, true, EpdFontFamily::BOLD);
  drawDefaultButtonHints(renderer, mappedInput);
  presentHud(renderer);
}

}  // namespace CompactHudRenderer
