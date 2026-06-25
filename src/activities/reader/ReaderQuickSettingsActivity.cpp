#include "ReaderQuickSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "activities/settings/FontSelectionActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int TAB_READER = 0;
constexpr int TAB_DISPLAY = 1;
constexpr int TAB_LAYOUT = 2;
constexpr int TAB_STATUS = 3;
constexpr int TAB_ADVANCED = 4;
constexpr int TAB_COUNT = 5;
constexpr int READER_ITEM_COUNT = 3;
constexpr int DISPLAY_ITEM_COUNT = 4;
constexpr int LAYOUT_ITEM_COUNT = 4;
constexpr int STATUS_ITEM_COUNT = 2;
constexpr int ADVANCED_ITEM_COUNT = 1;
constexpr int QUICK_SETTINGS_ACTION_DELETE_CACHE = 1;
constexpr int PANEL_RADIUS = 8;
constexpr int PANEL_PAD = 16;

constexpr StrId TAB_LABELS[TAB_COUNT] = {StrId::STR_CAT_READER, StrId::STR_CAT_DISPLAY, StrId::STR_CAT_LAYOUT,
                                         StrId::STR_CAT_STATUS, StrId::STR_CAT_ADVANCED};
constexpr StrId READER_ITEM_LABELS[READER_ITEM_COUNT] = {
    StrId::STR_FONT_FAMILY, StrId::STR_FONT_SIZE, StrId::STR_BIONIC_READING};
constexpr StrId DISPLAY_ITEM_LABELS[DISPLAY_ITEM_COUNT] = {StrId::STR_DARK_MODE, StrId::STR_TEXT_DARKNESS,
                                                           StrId::STR_READER_REFRESH_MODE, StrId::STR_IMAGES};
constexpr StrId LAYOUT_ITEM_LABELS[LAYOUT_ITEM_COUNT] = {StrId::STR_LINE_SPACING, StrId::STR_SCREEN_MARGIN,
                                                         StrId::STR_PARA_ALIGNMENT, StrId::STR_EXTRA_SPACING};
constexpr StrId STATUS_ITEM_LABELS[STATUS_ITEM_COUNT] = {StrId::STR_STATUS_BAR_POSITION,
                                                         StrId::STR_CUSTOMISE_STATUS_BAR};
constexpr StrId ADVANCED_ITEM_LABELS[ADVANCED_ITEM_COUNT] = {StrId::STR_DELETE_CACHE};

constexpr StrId FONT_FAMILY_LABELS[] = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS, StrId::STR_LEXEND};
constexpr StrId FONT_SIZE_LABELS[] = {StrId::STR_X_SMALL, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE,
                                      StrId::STR_X_LARGE};
constexpr StrId LINE_SPACING_LABELS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
constexpr StrId ALIGNMENT_LABELS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER,
                                      StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE};
constexpr StrId BIONIC_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE};
constexpr StrId DARK_MODE_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON};
constexpr StrId TEXT_DARKNESS_LABELS[] = {StrId::STR_NORMAL, StrId::STR_LEGACY_BW, StrId::STR_DARK,
                                          StrId::STR_EXTRA_DARK};
constexpr StrId REFRESH_MODE_LABELS[] = {StrId::STR_REFRESH_MODE_AUTO, StrId::STR_REFRESH_MODE_FAST,
                                         StrId::STR_REFRESH_MODE_HALF, StrId::STR_REFRESH_MODE_FULL};
constexpr StrId IMAGE_LABELS[] = {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER,
                                  StrId::STR_IMAGES_SUPPRESS};
constexpr StrId STATUS_BAR_PLACEMENT_LABELS[] = {StrId::STR_BOTTOM, StrId::STR_TOP, StrId::STR_HIDE};

uint8_t wrapEnum(const uint8_t value, const int direction, const uint8_t count) {
  if (count == 0) {
    return value;
  }
  if (direction > 0) {
    return static_cast<uint8_t>((value + 1) % count);
  }
  return value == 0 ? static_cast<uint8_t>(count - 1) : static_cast<uint8_t>(value - 1);
}

std::string enumValue(const uint8_t value, const StrId* labels, const uint8_t count) {
  return I18N.get(labels[std::min<uint8_t>(value, count - 1)]);
}

std::string currentFontValue() {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    return SETTINGS.sdFontFamilyName;
  }
  return enumValue(SETTINGS.fontFamily, FONT_FAMILY_LABELS, CrossPointSettings::BUILTIN_FONT_COUNT);
}

std::string valueForIndex(const int tab, const int index) {
  if (tab == TAB_READER) {
    switch (index) {
      case 0:
        return currentFontValue();
      case 1:
        return enumValue(SETTINGS.fontSize, FONT_SIZE_LABELS, CrossPointSettings::FONT_SIZE_COUNT);
      case 2:
        return enumValue(SETTINGS.bionicReading, BIONIC_LABELS, CrossPointSettings::BIONIC_READING_MODE_COUNT);
      default:
        return "";
    }
  }

  if (tab == TAB_DISPLAY) {
    switch (index) {
      case 0:
        return I18N.get(DARK_MODE_LABELS[SETTINGS.darkMode ? 1 : 0]);
      case 1:
        return enumValue(SETTINGS.textDarkness, TEXT_DARKNESS_LABELS, CrossPointSettings::TEXT_DARKNESS_COUNT);
      case 2:
        return enumValue(SETTINGS.readerRefreshMode, REFRESH_MODE_LABELS,
                         CrossPointSettings::READER_REFRESH_MODE_COUNT);
      case 3:
        return enumValue(SETTINGS.imageRendering, IMAGE_LABELS, CrossPointSettings::IMAGE_RENDERING_COUNT);
      default:
        return "";
    }
  }

  if (tab == TAB_LAYOUT) {
    switch (index) {
      case 0:
        return enumValue(SETTINGS.lineSpacing, LINE_SPACING_LABELS, CrossPointSettings::LINE_COMPRESSION_COUNT);
      case 1:
        return std::to_string(SETTINGS.screenMargin);
      case 2:
        return enumValue(SETTINGS.paragraphAlignment, ALIGNMENT_LABELS, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
      case 3:
        return I18N.get(DARK_MODE_LABELS[SETTINGS.extraParagraphSpacing ? 1 : 0]);
      default:
        return "";
    }
  }

  if (tab == TAB_STATUS && index == 0) {
    return enumValue(SETTINGS.statusBarPlacement, STATUS_BAR_PLACEMENT_LABELS,
                     CrossPointSettings::STATUS_BAR_PLACEMENT_COUNT);
  }

  return "";
}

StrId labelForIndex(const int tab, const int index) {
  switch (tab) {
    case TAB_READER:
      return READER_ITEM_LABELS[index];
    case TAB_DISPLAY:
      return DISPLAY_ITEM_LABELS[index];
    case TAB_LAYOUT:
      return LAYOUT_ITEM_LABELS[index];
    case TAB_STATUS:
      return STATUS_ITEM_LABELS[index];
    case TAB_ADVANCED:
      return ADVANCED_ITEM_LABELS[index];
    default:
      return READER_ITEM_LABELS[0];
  }
}
}  // namespace

void ReaderQuickSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedTab = TAB_READER;
  selectedIndex = 0;
  tabFocused = true;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate();
}

int ReaderQuickSettingsActivity::currentItemCount() const {
  switch (selectedTab) {
    case TAB_READER:
      return READER_ITEM_COUNT;
    case TAB_DISPLAY:
      return DISPLAY_ITEM_COUNT;
    case TAB_LAYOUT:
      return LAYOUT_ITEM_COUNT;
    case TAB_STATUS:
      return STATUS_ITEM_COUNT;
    case TAB_ADVANCED:
      return ADVANCED_ITEM_COUNT;
    default:
      return READER_ITEM_COUNT;
  }
}

void ReaderQuickSettingsActivity::adjustSelected(const int direction) {
  if (selectedTab == TAB_READER) {
    switch (selectedIndex) {
      case 0:
        return;
      case 1:
        SETTINGS.fontSize = wrapEnum(SETTINGS.fontSize, direction, CrossPointSettings::FONT_SIZE_COUNT);
        break;
      case 2:
        SETTINGS.bionicReading =
            wrapEnum(SETTINGS.bionicReading, direction, CrossPointSettings::BIONIC_READING_MODE_COUNT);
        break;
      default:
        return;
    }
  } else if (selectedTab == TAB_DISPLAY) {
    switch (selectedIndex) {
      case 0:
        SETTINGS.darkMode = SETTINGS.darkMode ? 0 : 1;
        renderer.setDarkMode(SETTINGS.darkMode);
        break;
      case 1:
        SETTINGS.textDarkness = wrapEnum(SETTINGS.textDarkness, direction, CrossPointSettings::TEXT_DARKNESS_COUNT);
        renderer.setTextDarkness(SETTINGS.textDarkness);
        break;
      case 2:
        SETTINGS.readerRefreshMode =
            wrapEnum(SETTINGS.readerRefreshMode, direction, CrossPointSettings::READER_REFRESH_MODE_COUNT);
        break;
      case 3:
        SETTINGS.imageRendering = wrapEnum(SETTINGS.imageRendering, direction, CrossPointSettings::IMAGE_RENDERING_COUNT);
        break;
      default:
        return;
    }
  } else if (selectedTab == TAB_LAYOUT) {
    switch (selectedIndex) {
      case 0:
        SETTINGS.lineSpacing = wrapEnum(SETTINGS.lineSpacing, direction, CrossPointSettings::LINE_COMPRESSION_COUNT);
        break;
      case 1:
        if (direction > 0) {
          SETTINGS.screenMargin = std::min<uint8_t>(40, SETTINGS.screenMargin + 5);
        } else {
          SETTINGS.screenMargin = SETTINGS.screenMargin <= 5 ? 40 : static_cast<uint8_t>(SETTINGS.screenMargin - 5);
        }
        break;
      case 2:
        SETTINGS.paragraphAlignment =
            wrapEnum(SETTINGS.paragraphAlignment, direction, CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
        break;
      case 3:
        SETTINGS.extraParagraphSpacing = SETTINGS.extraParagraphSpacing ? 0 : 1;
        break;
      default:
        return;
    }
  } else if (selectedTab == TAB_STATUS) {
    if (selectedIndex != 0) {
      return;
    }
    SETTINGS.statusBarPlacement =
        wrapEnum(SETTINGS.statusBarPlacement, direction, CrossPointSettings::STATUS_BAR_PLACEMENT_COUNT);
  } else {
    return;
  }
  SETTINGS.saveToFile();
}

void ReaderQuickSettingsActivity::openFontPicker() {
  startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                 FontSelectionActivity::Mode::Select),
                         [this](const ActivityResult&) {
                           SETTINGS.saveToFile();
                           sdFontSystem.ensureLoaded(renderer);
                           requestUpdate();
                         });
}

void ReaderQuickSettingsActivity::openStatusBarSettings() {
  startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void ReaderQuickSettingsActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }
  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!tabFocused) {
      tabFocused = true;
      requestUpdate();
      return;
    }
    finish();
    return;
  }

  if (tabFocused) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedTab = mappedInput.wasReleased(MappedInputManager::Button::Right)
                        ? ButtonNavigator::nextIndex(selectedTab, TAB_COUNT)
                        : ButtonNavigator::previousIndex(selectedTab, TAB_COUNT);
      selectedIndex = 0;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      selectedIndex = 0;
      tabFocused = false;
      requestUpdate();
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedTab == TAB_READER && selectedIndex == 0) {
      openFontPicker();
      return;
    }
    if (selectedTab == TAB_STATUS && selectedIndex == 1) {
      openStatusBarSettings();
      return;
    }
    if (selectedTab == TAB_ADVANCED && selectedIndex == 0) {
      setResult(MenuResult{QUICK_SETTINGS_ACTION_DELETE_CACHE, 0, 0});
      finish();
      return;
    }
    adjustSelected(1);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, currentItemCount());
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (selectedIndex <= 0) {
      selectedIndex = currentItemCount() - 1;
    } else {
      selectedIndex--;
    }
    requestUpdate();
    return;
  }
}

void ReaderQuickSettingsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int panelW = std::min(pageWidth - metrics.contentSidePadding * 2, std::max(350, pageWidth * 72 / 100));
  const int panelH = std::min(pageHeight - metrics.topPadding * 2 - metrics.buttonHintsHeight - 8, 330);
  const int panelX = (pageWidth - panelW) / 2;
  const int panelY = std::max(metrics.topPadding + 4, (pageHeight - metrics.buttonHintsHeight - panelH) / 2);
  const Rect panel{panelX, panelY, panelW, panelH};

  renderer.fillRoundedRect(panel.x, panel.y, panel.width, panel.height, PANEL_RADIUS, Color::White);
  renderer.drawRoundedRect(panel.x, panel.y, panel.width, panel.height, 1, PANEL_RADIUS, true);

  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_QUICK_SETTINGS), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, panel.x + std::max(0, (panel.width - titleW) / 2), panel.y + 12,
                    tr(STR_QUICK_SETTINGS), true, EpdFontFamily::BOLD);

  const int tabsTop = panel.y + 43;
  const int tabGap = 8;
  const int tabW = (panel.width - PANEL_PAD * 2 - tabGap) / TAB_COUNT;
  for (int tab = 0; tab < TAB_COUNT; ++tab) {
    const int tabX = panel.x + PANEL_PAD + tab * (tabW + tabGap);
    const bool active = tab == selectedTab;
    const bool focused = tabFocused && active;
    if (active) {
      renderer.fillRoundedRect(tabX, tabsTop, tabW, 28, 6, Color::LightGray);
    }
    renderer.drawRoundedRect(tabX, tabsTop, tabW, 28, focused ? 2 : 1, 6, true);
    const char* tabLabel = I18N.get(TAB_LABELS[tab]);
    const auto tabStyle = active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int tabTextW = renderer.getTextWidth(UI_10_FONT_ID, tabLabel, tabStyle);
    renderer.drawText(UI_10_FONT_ID, tabX + std::max(0, (tabW - tabTextW) / 2), tabsTop + 7, tabLabel, true,
                      tabStyle);
  }

  const int listTop = tabsTop + 38;
  const int rowHeight = 34;
  const int rowX = panel.x + PANEL_PAD;
  const int rowW = panel.width - PANEL_PAD * 2;
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  for (int index = 0; index < currentItemCount(); ++index) {
    const int rowY = listTop + index * rowHeight;
    if (rowY + rowHeight > panel.y + panel.height - 12) {
      break;
    }
    const bool selected = !tabFocused && index == selectedIndex;
    if (selected) {
      renderer.fillRoundedRect(rowX, rowY, rowW, rowHeight - 4, 6, Color::LightGray);
      renderer.drawRoundedRect(rowX, rowY, rowW, rowHeight - 4, 2, 6, true);
    }
    const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const std::string label = I18N.get(labelForIndex(selectedTab, index));
    const std::string value = valueForIndex(selectedTab, index);
    const int valueW = value.empty() ? 0 : renderer.getTextWidth(SMALL_FONT_ID, value.c_str());
    const int labelW = rowW - (value.empty() ? 20 : std::min(rowW / 2, valueW + 20));
    const auto labelLines = renderer.wrappedText(SMALL_FONT_ID, label.c_str(), labelW, 2, style);
    int textY = rowY + 5;
    for (const auto& line : labelLines) {
      renderer.drawText(SMALL_FONT_ID, rowX + 10, textY, line.c_str(), true, style);
      textY += lineH;
    }
    if (!value.empty()) {
      const std::string safeValue =
          renderer.truncatedText(SMALL_FONT_ID, value.c_str(), std::min(rowW / 2, valueW + 12));
      const int safeValueW = renderer.getTextWidth(SMALL_FONT_ID, safeValue.c_str());
      renderer.drawText(SMALL_FONT_ID, rowX + rowW - safeValueW - 10, rowY + 7, safeValue.c_str(), true);
    }
  }

  auto labels = tabFocused ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                           : mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  if (!tabFocused && selectedTab == TAB_READER && selectedIndex == 0) {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  } else if (!tabFocused && ((selectedTab == TAB_STATUS && selectedIndex == 1) ||
                             (selectedTab == TAB_ADVANCED && selectedIndex == 0))) {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
