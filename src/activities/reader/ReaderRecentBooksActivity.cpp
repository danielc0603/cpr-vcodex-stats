#include "ReaderRecentBooksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MAX_QUICK_RECENT_BOOKS = 5;

std::string recentBookTitle(const RecentBook& book) {
  if (!book.title.empty()) {
    return book.title;
  }
  const size_t slash = book.path.find_last_of('/');
  return slash == std::string::npos ? book.path : book.path.substr(slash + 1);
}

std::string recentBookSubtitle(const RecentBook& book) {
  const auto* stats = READING_STATS.findBook(book.path);
  std::string subtitle = book.author;
  if (stats != nullptr) {
    const std::string progress = std::to_string(stats->lastProgressPercent) + "%";
    if (!subtitle.empty()) {
      subtitle += "  ";
    }
    subtitle += progress;
  }
  return subtitle;
}
}  // namespace

ReaderRecentBooksActivity::ReaderRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const std::string& currentPath)
    : Activity("ReaderRecentBooks", renderer, mappedInput), currentPath(currentPath) {}

void ReaderRecentBooksActivity::loadBooks() {
  books.clear();
  books.reserve(MAX_QUICK_RECENT_BOOKS);
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (book.path.empty() || book.path == currentPath) {
      continue;
    }
    books.push_back(book);
    if (books.size() >= MAX_QUICK_RECENT_BOOKS) {
      break;
    }
  }
  selectedIndex = 0;
}

void ReaderRecentBooksActivity::onEnter() {
  Activity::onEnter();
  loadBooks();
  waitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate();
}

void ReaderRecentBooksActivity::loop() {
  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (books.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
    setResult(KeyboardResult{books[selectedIndex].path});
    finish();
    return;
  }

  const int total = static_cast<int>(books.size());
  buttonNavigator.onNext([this, total] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, total] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });
}

void ReaderRecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentWidth = pageWidth - sidePadding * 2;
  renderer.drawCenteredText(UI_12_FONT_ID, metrics.topPadding + 10, tr(STR_RECENT_BOOKS), true, EpdFontFamily::BOLD);

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, metrics.topPadding + metrics.headerHeight + 30, tr(STR_NO_OPEN_BOOK));
  } else {
    constexpr int rowHeight = 54;
    constexpr int rowGap = 8;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
    for (int index = 0; index < static_cast<int>(books.size()); ++index) {
      const bool selected = index == selectedIndex;
      const int rowBottom = y + rowHeight;
      if (rowBottom > renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing) {
        break;
      }
      if (selected) {
        renderer.fillRectDither(sidePadding, y - 4, contentWidth, rowHeight, Color::LightGray);
      }
      renderer.drawRect(sidePadding, y - 4, contentWidth, rowHeight);
      const std::string title =
          renderer.truncatedText(UI_10_FONT_ID, recentBookTitle(books[index]).c_str(), contentWidth - 24,
                                 EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, sidePadding + 12, y + 4, title.c_str(), true, EpdFontFamily::BOLD);
      const std::string subtitle = recentBookSubtitle(books[index]);
      if (!subtitle.empty()) {
        const std::string safeSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle.c_str(), contentWidth - 24);
        renderer.drawText(SMALL_FONT_ID, sidePadding + 12, y + 27, safeSubtitle.c_str());
      }
      y += rowHeight + rowGap;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
