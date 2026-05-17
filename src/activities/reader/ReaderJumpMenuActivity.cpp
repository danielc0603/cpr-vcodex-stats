#include "ReaderJumpMenuActivity.h"

#include <GfxRenderer.h>

#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

ReaderJumpMenuActivity::ReaderJumpMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const bool hasChapters,
                                               const bool hasBookmarks)
    : Activity("ReaderJumpMenu", renderer, mappedInput), title(title) {
  items.reserve(4);
  if (hasChapters) {
    items.push_back({Action::CHAPTERS, StrId::STR_SELECT_CHAPTER});
  }
  items.push_back({Action::PERCENT, StrId::STR_GO_TO_PERCENT});
  if (hasBookmarks) {
    items.push_back({Action::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({Action::BACK_TO_READING, StrId::STR_BACK_TO_READING});
}

void ReaderJumpMenuActivity::onEnter() {
  Activity::onEnter();
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate();
}

void ReaderJumpMenuActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(MenuResult{static_cast<int>(items[selectedIndex].action), 0, 0});
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }
}

void ReaderJumpMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentWidth = pageWidth - sidePadding * 2;
  const std::string safeTitle = renderer.truncatedText(UI_12_FONT_ID, title.c_str(), contentWidth, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + 10, safeTitle.c_str(), true, EpdFontFamily::BOLD);

  constexpr int rowHeight = 34;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 12;
  for (int index = 0; index < static_cast<int>(items.size()); ++index) {
    const bool selected = index == selectedIndex;
    if (selected) {
      renderer.fillRect(sidePadding, y - 4, contentWidth, rowHeight, true);
    }
    renderer.drawText(UI_10_FONT_ID, sidePadding + 12, y, I18N.get(items[index].labelId), !selected,
                      EpdFontFamily::BOLD);
    y += rowHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
