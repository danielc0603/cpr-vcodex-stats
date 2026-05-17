#include "ReaderBookInfoActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

ReaderBookInfoActivity::ReaderBookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string title)
    : Activity("ReaderBookInfo", renderer, mappedInput), title(std::move(title)) {}

void ReaderBookInfoActivity::onEnter() {
  Activity::onEnter();
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate();
}

void ReaderBookInfoActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }
}

void ReaderBookInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentWidth = pageWidth - sidePadding * 2;
  int y = metrics.topPadding + 10;

  renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_BOOK_INFO), true, EpdFontFamily::BOLD);
  y += metrics.headerHeight + metrics.verticalSpacing;

  const auto titleLines =
      renderer.wrappedText(UI_10_FONT_ID, title.c_str(), contentWidth, 2, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  y += 14;

  const auto bodyLines =
      renderer.wrappedText(UI_10_FONT_ID, tr(STR_BOOK_INFO_PLACEHOLDER), contentWidth, 4);
  for (const auto& line : bodyLines) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, y, line.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
