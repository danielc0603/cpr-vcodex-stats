#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Update.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MIN_FIRMWARE_SIZE = 128 * 1024;
constexpr size_t INSTALL_BUFFER_SIZE = 4096;

std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool endsWithBin(const std::string& path) {
  const std::string lowered = lowerCopy(path);
  return lowered.size() > 4 && lowered.rfind(".bin") == lowered.size() - 4;
}

std::string basename(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string joinPath(const char* folder, const String& item) {
  std::string path = item.c_str();
  if (!path.empty() && path[0] == '/') {
    return path;
  }
  std::string base = folder;
  if (base.empty()) base = "/";
  if (base.size() > 1 && base.back() == '/') base.pop_back();
  return base == "/" ? "/" + path : base + "/" + path;
}
}  // namespace

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  discoverFirmwareFiles();
  state = firmwareFiles.empty() ? FAILED : PICK_FILE;
  statusText = firmwareFiles.empty() ? tr(STR_NO_FILES_FOUND) : "";
  selectedIndex = 0;
  installPercent = 0;
  requestUpdate();
}

void SdFirmwareUpdateActivity::discoverFirmwareFiles() {
  firmwareFiles.clear();
  scanFolder("/");
  scanFolder("/firmware");
  scanFolder("/updates");
  std::sort(firmwareFiles.begin(), firmwareFiles.end());
  firmwareFiles.erase(std::unique(firmwareFiles.begin(), firmwareFiles.end()), firmwareFiles.end());
}

void SdFirmwareUpdateActivity::scanFolder(const char* folder) {
  if (!Storage.exists(folder)) {
    return;
  }
  for (const auto& item : Storage.listFiles(folder, 80)) {
    const std::string path = joinPath(folder, item);
    if (endsWithBin(path)) {
      firmwareFiles.push_back(path);
    }
  }
}

const std::string& SdFirmwareUpdateActivity::selectedPath() const {
  static const std::string empty;
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(firmwareFiles.size())) {
    return empty;
  }
  return firmwareFiles[selectedIndex];
}

void SdFirmwareUpdateActivity::moveSelection(const int delta) {
  if (firmwareFiles.empty()) return;
  selectedIndex += delta;
  if (selectedIndex < 0) selectedIndex = static_cast<int>(firmwareFiles.size()) - 1;
  if (selectedIndex >= static_cast<int>(firmwareFiles.size())) selectedIndex = 0;
  requestUpdate();
}

bool SdFirmwareUpdateActivity::installSelectedFirmware() {
  const std::string path = selectedPath();
  if (path.empty() || !endsWithBin(path)) {
    statusText = tr(STR_UPDATE_FAILED);
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("SDU", path, file)) {
    statusText = tr(STR_UPDATE_FAILED);
    return false;
  }

  const size_t firmwareSize = file.fileSize();
  if (firmwareSize < MIN_FIRMWARE_SIZE) {
    file.close();
    statusText = tr(STR_UPDATE_FAILED);
    return false;
  }

  if (!Update.begin(firmwareSize, U_FLASH)) {
    file.close();
    statusText = tr(STR_UPDATE_FAILED);
    return false;
  }

  uint8_t buffer[INSTALL_BUFFER_SIZE];
  size_t written = 0;
  while (written < firmwareSize) {
    const size_t remaining = firmwareSize - written;
    const size_t chunkSize = std::min(sizeof(buffer), remaining);
    const int readLen = file.read(buffer, chunkSize);
    if (readLen <= 0) {
      Update.abort();
      file.close();
      statusText = tr(STR_UPDATE_FAILED);
      return false;
    }
    const size_t updateWritten = Update.write(buffer, static_cast<size_t>(readLen));
    if (updateWritten != static_cast<size_t>(readLen)) {
      Update.abort();
      file.close();
      statusText = tr(STR_UPDATE_FAILED);
      return false;
    }
    written += updateWritten;
    installPercent = static_cast<int>((written * 100) / firmwareSize);
    requestUpdate(true);
    delay(1);
  }

  file.close();
  if (!Update.end(true) || !Update.isFinished()) {
    statusText = tr(STR_UPDATE_FAILED);
    return false;
  }
  statusText = tr(STR_UPDATE_COMPLETE);
  return true;
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));

  if (state == PICK_FILE) {
    const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int maxRows = std::max(1, (pageHeight - listTop - 32) / (lineHeight + 4));
    const int first = std::max(0, std::min(selectedIndex - maxRows / 2,
                                           static_cast<int>(firmwareFiles.size()) - maxRows));
    for (int row = 0; row < maxRows && first + row < static_cast<int>(firmwareFiles.size()); ++row) {
      const int index = first + row;
      const int y = listTop + row * (lineHeight + 4);
      if (index == selectedIndex) {
        renderer.drawRect(4, y - 2, pageWidth - 8, lineHeight + 4, true);
      }
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, basename(firmwareFiles[index]).c_str(),
                        index == selectedIndex, index == selectedIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == CONFIRM_INSTALL) {
    const int top = pageHeight / 2 - lineHeight * 2;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing,
                              basename(selectedPath()).c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == INSTALLING) {
    const int top = pageHeight / 2 - lineHeight;
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, top + lineHeight + metrics.verticalSpacing,
                             pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                        installPercent, 100);
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - lineHeight, tr(STR_UPDATE_COMPLETE), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + metrics.verticalSpacing, tr(STR_POWER_ON_HINT));
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - lineHeight, tr(STR_UPDATE_FAILED), true,
                              EpdFontFamily::BOLD);
    if (!statusText.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + metrics.verticalSpacing, statusText.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

void SdFirmwareUpdateActivity::loop() {
  if (state == PICK_FILE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      moveSelection(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      moveSelection(1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !firmwareFiles.empty()) {
      state = CONFIRM_INSTALL;
      requestUpdate();
      return;
    }
  }

  if (state == CONFIRM_INSTALL) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = PICK_FILE;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state = INSTALLING;
      requestUpdateAndWait();
      state = installSelectedFirmware() ? SUCCESS : FAILED;
      requestUpdateAndWait();
      if (state == SUCCESS) {
        delay(3000);
        ESP.restart();
      }
      return;
    }
  }

  if (state == FAILED && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}
