#include "ConfirmationActivity.h"

#include <I18n.h>

#include <algorithm>

#include "CompactHudRenderer.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();
  selectedAction = 1;

  lineHeight = renderer.getLineHeight(fontId);
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  panelWidth = std::min(pageWidth - margin * 2, std::max(220, pageWidth * 2 / 3));
  const int innerWidth = panelWidth - 28;

  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(fontId, heading.c_str(), innerWidth, EpdFontFamily::BOLD);
  }
  if (!body.empty()) {
    bodyLines = renderer.wrappedText(fontId, body.c_str(), innerWidth, 3);
    safeBody = bodyLines.empty() ? "" : bodyLines.front();
  }

  panelHeight = 34;
  if (!safeHeading.empty()) panelHeight += lineHeight + spacing;
  panelHeight += std::max(1, static_cast<int>(bodyLines.size())) * (lineHeight + 2);
  panelHeight += 46;

  panelX = (pageWidth - panelWidth) / 2;
  panelY = (pageHeight - panelHeight) / 2;

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  CompactHudRenderer::ConfirmationConfig config;
  config.title = safeHeading.empty() ? heading : safeHeading;
  config.body = bodyLines;
  config.selectedAction = selectedAction;
  CompactHudRenderer::drawConfirmation(renderer, mappedInput, config);
}

void ConfirmationActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedAction = 1 - selectedAction;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ActivityResult res;
    res.isCancelled = selectedAction == 0;
    setResult(std::move(res));
    finish();
    return;
  }
}
