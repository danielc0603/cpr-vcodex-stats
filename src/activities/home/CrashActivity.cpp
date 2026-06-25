#include "CrashActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char LIBRARY_BREADCRUMB_FILE[] = "/.crosspoint/library_crash_breadcrumb.txt";

std::string trimCrashText(const std::string& value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return ch == ' ' || ch == '\t'; });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return ch == ' ' || ch == '\t'; }).base();
  if (first >= last) {
    return "";
  }
  return std::string(first, last);
}

std::string firstCrashLine(const std::string& text) {
  const size_t end = text.find_first_of("\r\n");
  return trimCrashText(end == std::string::npos ? text : text.substr(0, end));
}

std::string breadcrumbValue(const std::string& text, const char* key) {
  const std::string prefix = std::string(key) + "=";
  size_t pos = text.find(prefix);
  if (pos == std::string::npos) {
    return "";
  }
  pos += prefix.size();
  const size_t end = text.find_first_of("\r\n", pos);
  return trimCrashText(text.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
}

std::string basenameForCrash(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string shortenCrashValue(const std::string& value, const size_t maxChars) {
  if (value.size() <= maxChars) {
    return value;
  }
  constexpr char ellipsis[] = "...";
  if (maxChars <= 3) {
    return value.substr(0, maxChars);
  }
  return std::string(ellipsis) + value.substr(value.size() - (maxChars - 3));
}

std::string layoutNameForCrash(const std::string& value) {
  if (value == "0") return "3x3";
  if (value == "1" || value == "2" || value == "3") return "stale -> 3x3";
  return value;
}

bool containsCrashText(const std::string& text, const char* token) { return text.find(token) != std::string::npos; }

std::string parseCrashReason(const std::string& text) {
  const std::string firstLine = firstCrashLine(text);
  if (containsCrashText(text, "assert failed")) return "Assertion Failed";
  if (containsCrashText(text, "LoadProhibited")) return "Load Prohibited";
  if (containsCrashText(text, "StoreProhibited")) return "Store Prohibited";
  if (containsCrashText(text, "stack overflow")) return "Stack Overflow";
  if (containsCrashText(text, "watchdog") || containsCrashText(text, "Watchdog")) return "Watchdog Timeout";
  if (containsCrashText(text, "abort")) return "Abort";
  if (containsCrashText(text, "allocation") || containsCrashText(text, "malloc")) return "Failed Allocation";
  if (containsCrashText(text, "Guru Meditation")) return "Guru Meditation";
  return firstLine.empty() ? std::string(tr(STR_CRASH_NO_REASON)) : firstLine;
}

std::string parseCrashLocation(const std::string& text) {
  const size_t assertPos = text.find("assert failed:");
  if (assertPos != std::string::npos) {
    const size_t start = assertPos + strlen("assert failed:");
    const size_t end = text.find_first_of(" \r\n", start);
    const std::string fn = trimCrashText(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (!fn.empty()) return fn;
  }
  if (containsCrashText(text, "xTaskPriorityDisinherit")) return "xTaskPriorityDisinherit()";
  if (containsCrashText(text, "FileBrowserActivity")) return "FileBrowserActivity";
  if (containsCrashText(text, "ReadingStats")) return "Reading Stats";
  if (containsCrashText(text, "ActivityManager")) return "ActivityManager";
  return "Unknown";
}

std::string readLibraryBreadcrumb() {
  if (!Storage.exists(LIBRARY_BREADCRUMB_FILE)) {
    return "";
  }
  const String value = Storage.readFile(LIBRARY_BREADCRUMB_FILE);
  const std::string raw = trimCrashText(std::string(value.c_str()));
  if (raw.empty()) {
    return "";
  }

  const std::string phase = breadcrumbValue(raw, "phase");
  if (phase == "closed") {
    return "";
  }

  std::string formatted;
  const std::string count = breadcrumbValue(raw, "count");
  const std::string index = breadcrumbValue(raw, "index");
  const std::string layout = breadcrumbValue(raw, "layout");
  const std::string path = breadcrumbValue(raw, "path");
  const std::string book = breadcrumbValue(raw, "book");

  if (!phase.empty()) formatted += "Phase: " + phase + "\n";
  if (!count.empty()) formatted += "Count: " + count + "\n";
  if (!index.empty()) formatted += "Index: " + index + "\n";
  if (!layout.empty()) formatted += "Layout: " + layoutNameForCrash(layout) + "\n";
  if (!path.empty()) formatted += "Path: " + shortenCrashValue(path, 36) + "\n";
  if (!book.empty()) formatted += "Book: " + shortenCrashValue(basenameForCrash(book), 36);

  return formatted.empty() ? raw : trimCrashText(formatted);
}
}  // namespace

void CrashActivity::onEnter() {
  Activity::onEnter();

  panicMessage = HalSystem::getPanicInfo(false);
  if (panicMessage.empty()) {
    panicMessage = tr(STR_CRASH_NO_REASON);
  }
  crashReason = parseCrashReason(panicMessage);
  crashLocation = parseCrashLocation(panicMessage);
  libraryBreadcrumb = readLibraryBreadcrumb();
  HalSystem::clearPanic();

  requestUpdateAndWait();
}

void CrashActivity::loop() {
  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void CrashActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const auto x = metrics.contentSidePadding;
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CRASH_TITLE));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  auto descLines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_CRASH_DESCRIPTION), contentWidth, 10);
  for (const auto& line : descLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }

  y += metrics.verticalSpacing * 2;
  renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_CRASH_REASON));
  y += lineHeight + metrics.verticalSpacing;

  auto reasonLines = renderer.wrappedText(UI_10_FONT_ID, crashReason.c_str(), contentWidth, 2);
  for (const auto& line : reasonLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += lineHeight;
  }

  y += metrics.verticalSpacing;
  renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_CRASH_LOCATION));
  y += lineHeight + metrics.verticalSpacing;

  auto locationLines = renderer.wrappedText(UI_10_FONT_ID, crashLocation.c_str(), contentWidth, 2);
  for (const auto& line : locationLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += lineHeight;
  }

  y += metrics.verticalSpacing;
  if (!libraryBreadcrumb.empty()) {
    renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_CRASH_LIBRARY_BREADCRUMB));
    y += lineHeight + metrics.verticalSpacing;

    auto breadcrumbLines = renderer.wrappedText(SMALL_FONT_ID, libraryBreadcrumb.c_str(), contentWidth, 5);
    const auto smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    for (const auto& line : breadcrumbLines) {
      renderer.drawText(SMALL_FONT_ID, x, y, line.c_str(), true);
      y += smallLineHeight;
    }
    y += metrics.verticalSpacing;
  }

  const auto smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  auto panicLines = renderer.wrappedText(SMALL_FONT_ID, panicMessage.c_str(), contentWidth, 4);
  for (const auto& line : panicLines) {
    renderer.drawText(SMALL_FONT_ID, x, y, line.c_str());
    y += smallLineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
