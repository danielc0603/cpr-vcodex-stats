#include "FontSelectionActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontInstaller.h"
#include "MappedInputManager.h"
#include "SdCardFont.h"
#include "SdCardFontGlobals.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
#define FONTS_MANIFEST_VERSION 1
#ifndef FONT_MANIFEST_URL
#define FONT_MANIFEST_URL_STRINGIFY_INNER(x) #x
#define FONT_MANIFEST_URL_STRINGIFY(x) FONT_MANIFEST_URL_STRINGIFY_INNER(x)
#define FONT_MANIFEST_URL                                                                                           \
  "https://github.com/crosspoint-reader/crosspoint-fonts/releases/download/sd-fonts-m" FONT_MANIFEST_URL_STRINGIFY( \
      FONTS_MANIFEST_VERSION) "-b" FONT_MANIFEST_URL_STRINGIFY(CPFONT_VERSION) "/fonts.json"
#endif

constexpr int MANAGER_HOME_COUNT = 2;
constexpr StrId MANAGER_HOME_LABELS[MANAGER_HOME_COUNT] = {StrId::STR_INSTALLED_FONTS, StrId::STR_DOWNLOAD_FONTS};

std::string shortStatus(const std::string& text) {
  return text.size() > 28 ? text.substr(0, 28) : text;
}
}  // namespace

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry, Mode mode)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry), mode_(mode) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));

  fonts_.push_back({I18N.get(StrId::STR_BOOKERLY), true, CrossPointSettings::BOOKERLY});
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, CrossPointSettings::NOTOSANS});
  fonts_.push_back({I18N.get(StrId::STR_LEXEND), true, CrossPointSettings::LEXEND});

  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  selectedIndex_ = 0;
  if (mode_ == Mode::Select) {
    if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
      const auto& families = registry_->getFamilies();
      for (int i = 0; i < static_cast<int>(families.size()); i++) {
        if (families[i].name == SETTINGS.sdFontFamilyName) {
          selectedIndex_ = CrossPointSettings::BUILTIN_FONT_COUNT + i;
          break;
        }
      }
    } else {
      selectedIndex_ = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
    }
  }

  requestUpdate();
}

void FontSelectionActivity::onExit() {
  Activity::onExit();
  if (mode_ == Mode::Manage) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
}

int FontSelectionActivity::currentListSize() const {
  if (mode_ == Mode::Select) return static_cast<int>(fonts_.size());
  if (managerView_ == ManagerView::Home) return MANAGER_HOME_COUNT;
  if (managerView_ == ManagerView::Installed) {
    return std::max(0, static_cast<int>(fonts_.size()) - static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT));
  }
  return downloadState_ == DownloadState::Ready ? static_cast<int>(catalog_.size()) : 0;
}

void FontSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (mode_ == Mode::Manage && managerView_ != ManagerView::Home) {
      leaveManagerView();
      return;
    }
    finish();
    return;
  }

  if (downloadState_ == DownloadState::Loading || downloadState_ == DownloadState::Downloading) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (mode_ == Mode::Select) {
      handleSelection();
      return;
    }

    if (managerView_ == ManagerView::Home) {
      enterManagerView(selectedIndex_ == 0 ? ManagerView::Installed : ManagerView::Download);
      return;
    }
    if (managerView_ == ManagerView::Installed) {
      handleSelection();
      return;
    }
    if (downloadState_ == DownloadState::NotLoaded || downloadState_ == DownloadState::Error) {
      startCatalogLoad();
      return;
    }
    if (downloadState_ == DownloadState::Complete) {
      downloadState_ = DownloadState::Ready;
      requestUpdate();
      return;
    }
    downloadSelectedCatalogFont();
    return;
  }

  if (mode_ == Mode::Manage && managerView_ == ManagerView::Installed && mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    confirmUninstall();
    return;
  }

  const int listSize = currentListSize();
  if (listSize <= 0) return;

  buttonNavigator_.onNextRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });
}

bool FontSelectionActivity::selectedFontIsCustom() const {
  const int fontIndex =
      mode_ == Mode::Manage && managerView_ == ManagerView::Installed
          ? selectedIndex_ + static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT)
          : selectedIndex_;
  return fontIndex >= 0 && fontIndex < static_cast<int>(fonts_.size()) && !fonts_[fontIndex].isBuiltin;
}

void FontSelectionActivity::handleSelection() {
  const int fontIndex =
      mode_ == Mode::Manage && managerView_ == ManagerView::Installed
          ? selectedIndex_ + static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT)
          : selectedIndex_;
  if (fontIndex < 0 || fontIndex >= static_cast<int>(fonts_.size())) return;

  const auto& font = fonts_[fontIndex];
  if (font.settingIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
  finish();
}

void FontSelectionActivity::confirmUninstall() {
  if (!selectedFontIsCustom() || registry_ == nullptr) return;
  const int fontIndex = selectedIndex_ + static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE), fonts_[fontIndex].name),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) handleUninstall();
        requestUpdate();
      });
}

void FontSelectionActivity::handleUninstall() {
  if (!selectedFontIsCustom() || registry_ == nullptr) return;

  FontInstaller installer(sdFontSystem.registry());
  const int fontIndex = selectedIndex_ + static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT);
  if (fontIndex < 0 || fontIndex >= static_cast<int>(fonts_.size())) return;

  const auto result = installer.deleteFamily(fonts_[fontIndex].name.c_str());
  if (result != FontInstaller::Error::OK) {
    LOG_ERR("FONTUI", "Failed to uninstall font family: %s", fonts_[fontIndex].name.c_str());
    statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
    downloadState_ = DownloadState::Error;
    return;
  }

  sdFontSystem.markRegistryDirty();
  sdFontSystem.refreshIfDirty();
  onEnter();
  managerView_ = ManagerView::Installed;
}

void FontSelectionActivity::enterManagerView(const ManagerView view) {
  managerView_ = view;
  selectedIndex_ = 0;
  requestUpdate();
}

void FontSelectionActivity::leaveManagerView() {
  managerView_ = ManagerView::Home;
  selectedIndex_ = 0;
  requestUpdate();
}

void FontSelectionActivity::startCatalogLoad() {
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(result); });
}

void FontSelectionActivity::onWifiSelectionComplete(const ActivityResult& result) {
  if (result.isCancelled) {
    statusMessage_ = tr(STR_CONNECTION_FAILED);
    downloadState_ = DownloadState::Error;
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    downloadState_ = DownloadState::Loading;
    statusMessage_ = tr(STR_LOADING_FONT_LIST);
  }
  requestUpdateAndWait();

  if (fetchCatalog()) {
    downloadState_ = DownloadState::Ready;
    selectedIndex_ = 0;
  } else {
    downloadState_ = DownloadState::Error;
  }
  requestUpdate();
}

bool FontSelectionActivity::fetchCatalog() {
  static constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";
  const auto result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
  if (result != HttpDownloader::OK) {
    statusMessage_ = tr(STR_DOWNLOAD_FAILED);
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  FsFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    statusMessage_ = tr(STR_DOWNLOAD_FAILED);
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, manifestFile);
  manifestFile.close();
  Storage.remove(MANIFEST_TMP);
  if (err) {
    statusMessage_ = tr(STR_INVALID_FONT_CATALOG);
    return false;
  }

  if ((doc["version"] | 0) != FONTS_MANIFEST_VERSION) {
    statusMessage_ = tr(STR_INVALID_FONT_CATALOG);
    return false;
  }

  catalogBaseUrl_ = doc["baseUrl"] | "";
  catalog_.clear();
  sdFontSystem.refreshIfDirty();
  FontInstaller installer(sdFontSystem.registry());

  for (JsonObject fObj : doc["families"].as<JsonArray>()) {
    CatalogFamily family;
    family.name = fObj["name"] | "";
    family.description = fObj["description"] | "";
    if (!FontInstaller::isValidFamilyName(family.name.c_str())) continue;

    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      CatalogFile file;
      file.name = fileObj["name"] | "";
      file.size = fileObj["size"] | 0;
      if (!FontInstaller::isValidCpfontFilename(file.name.c_str())) continue;
      family.totalSize += file.size;
      family.files.push_back(std::move(file));
    }

    if (!family.files.empty()) {
      family.installed = installer.isFamilyInstalled(family.name.c_str());
      catalog_.push_back(std::move(family));
    }
  }

  if (catalog_.empty()) {
    statusMessage_ = tr(STR_NO_FONTS_AVAILABLE);
    return false;
  }
  return true;
}

void FontSelectionActivity::downloadSelectedCatalogFont() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(catalog_.size())) return;

  CatalogFamily& family = catalog_[selectedIndex_];
  FontInstaller installer(sdFontSystem.registry());
  if (!installer.ensureFamilyDir(family.name.c_str())) {
    statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
    downloadState_ = DownloadState::Error;
    requestUpdate();
    return;
  }

  downloadState_ = DownloadState::Downloading;
  downloadFileIndex_ = 0;
  downloadFileCount_ = family.files.size();
  downloadProgress_ = 0;
  downloadTotal_ = 0;
  requestUpdateAndWait();

  for (size_t i = 0; i < family.files.size(); ++i) {
    const auto& file = family.files[i];
    downloadFileIndex_ = i;
    downloadProgress_ = 0;
    downloadTotal_ = file.size;
    requestUpdateAndWait();

    char destPath[160];
    FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), destPath, sizeof(destPath));
    const std::string url = catalogBaseUrl_ + file.name;
    const auto result = HttpDownloader::downloadToFile(url, destPath, [this](size_t done, size_t total) {
      downloadProgress_ = done;
      downloadTotal_ = total;
      requestUpdate(true);
    });

    if (result != HttpDownloader::OK || !installer.validateCpfontFile(destPath)) {
      installer.deleteFamily(family.name.c_str());
      family.installed = false;
      statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
      downloadState_ = DownloadState::Error;
      requestUpdate();
      return;
    }
  }

  installer.refreshRegistry();
  sdFontSystem.markRegistryDirty();
  sdFontSystem.refreshIfDirty();
  family.installed = true;
  statusMessage_ = tr(STR_FONT_INSTALLED);
  downloadState_ = DownloadState::Complete;
  requestUpdate();
}

std::string FontSelectionActivity::currentDownloadValue(const int index) const {
  if (index < 0 || index >= static_cast<int>(catalog_.size())) return "";
  const auto& family = catalog_[index];
  return family.installed ? tr(STR_INSTALLED) : formatSize(family.totalSize);
}

std::string FontSelectionActivity::formatSize(const size_t bytes) {
  char buf[24];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const char* title = mode_ == Mode::Manage ? tr(STR_FONT_MANAGER) : tr(STR_FONT_FAMILY);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (mode_ == Mode::Manage && managerView_ == ManagerView::Home) {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, MANAGER_HOME_COUNT, selectedIndex_,
                 [](int index) { return std::string(I18N.get(MANAGER_HOME_LABELS[index])); }, nullptr, nullptr,
                 nullptr, false);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (mode_ == Mode::Manage && managerView_ == ManagerView::Download) {
    if (downloadState_ != DownloadState::Ready) {
      const char* message = statusMessage_.empty() ? tr(STR_DOWNLOAD_FONTS) : statusMessage_.c_str();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2, message,
                                true, EpdFontFamily::BOLD);
      if (downloadState_ == DownloadState::Downloading) {
        const std::string progress =
            std::to_string(downloadFileIndex_ + 1) + "/" + std::to_string(downloadFileCount_) + " " +
            formatSize(downloadProgress_) + "/" + formatSize(downloadTotal_);
        renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 18, progress.c_str());
      }
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), downloadState_ == DownloadState::Complete ? tr(STR_OK_BUTTON) : tr(STR_DOWNLOAD),
                                                "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer();
      return;
    }

    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(catalog_.size()),
                 selectedIndex_, [this](int index) { return catalog_[index].name; },
                 [this](int index) { return shortStatus(catalog_[index].description); }, nullptr,
                 [this](int index) { return currentDownloadValue(index); }, true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DOWNLOAD), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  int currentFontIndex = 0;
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        currentFontIndex = CrossPointSettings::BUILTIN_FONT_COUNT + i;
        break;
      }
    }
  } else {
    currentFontIndex = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  }

  if (mode_ == Mode::Manage && managerView_ == ManagerView::Installed && currentListSize() == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2,
                              tr(STR_NO_CUSTOM_FONTS), true, EpdFontFamily::BOLD);
  } else {
    const bool customOnly = mode_ == Mode::Manage && managerView_ == ManagerView::Installed;
    const int itemCount = customOnly ? currentListSize() : static_cast<int>(fonts_.size());
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex_,
        [this, customOnly](int index) {
          const int fontIndex = customOnly ? index + static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT) : index;
          return fonts_[fontIndex].name;
        },
        nullptr, nullptr,
        [this, currentFontIndex, customOnly](int index) -> std::string {
          const int fontIndex = customOnly ? index + static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT) : index;
          return fontIndex == currentFontIndex ? tr(STR_SELECTED) : "";
        },
        true);
  }

  const auto labels = mode_ == Mode::Manage && selectedFontIsCustom()
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DELETE))
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
