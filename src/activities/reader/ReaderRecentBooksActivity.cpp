#include "ReaderRecentBooksActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RecentBooksGrid.h"

namespace {
constexpr int MAX_QUICK_RECENT_BOOKS = 6;

}  // namespace

ReaderRecentBooksActivity::ReaderRecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const std::string& currentPath)
    : Activity("ReaderRecentBooks", renderer, mappedInput), currentPath(currentPath) {}

void ReaderRecentBooksActivity::loadBooks() {
  books.clear();
  books.reserve(MAX_QUICK_RECENT_BOOKS);
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (book.path.empty() || book.path == currentPath || !Storage.exists(book.path.c_str())) {
      continue;
    }
    RecentBook resolvedBook = book;
    const auto* statsBook = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
    if (resolvedBook.coverBmpPath.empty() && statsBook != nullptr && !statsBook->coverBmpPath.empty()) {
      resolvedBook.coverBmpPath = statsBook->coverBmpPath;
    }
    books.push_back(RecentBooksGrid::BookState{resolvedBook});
    if (books.size() >= MAX_QUICK_RECENT_BOOKS) {
      break;
    }
  }
  selectedIndex = 0;
  loadedPageStart = -1;
  loadVisiblePageMetadata();
}

void ReaderRecentBooksActivity::onEnter() {
  Activity::onEnter();
  loadBooks();
  mappedInput.armPressedButtonsReleaseGuard();
  waitForInputRelease = mappedInput.isAnyMappedButtonPressed();
  requestUpdate();
}

void ReaderRecentBooksActivity::loop() {
  if (waitForInputRelease) {
    if (!mappedInput.isAnyMappedButtonPressed()) {
      waitForInputRelease = false;
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
    setResult(KeyboardResult{books[selectedIndex].book.path});
    finish();
    return;
  }

  const int total = static_cast<int>(books.size());
  if (total <= 0) {
    return;
  }
  const int pageItems = RecentBooksGrid::itemsPerPageForCount(total, RecentBooksGrid::kQuickItemsPerPage);
  auto moveSelection = [this, total](const int next) {
    selectedIndex = next;
    loadVisiblePageMetadata();
    requestUpdate();
  };
  buttonNavigator.onPress({MappedInputManager::Button::Right}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, true));
  });
  buttonNavigator.onPress({MappedInputManager::Button::Left}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, false));
  });
  buttonNavigator.onPress({MappedInputManager::Button::Down}, [this, total, pageItems, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, pageItems, true));
  });
  buttonNavigator.onPress({MappedInputManager::Button::Up}, [this, total, pageItems, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, pageItems, false));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Right}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, true));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Left}, [this, total, moveSelection] {
    moveSelection(RecentBooksGrid::moveHorizontal(selectedIndex, total, false));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, total, pageItems, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, pageItems, true));
  });
  buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, total, pageItems, moveSelection] {
    moveSelection(RecentBooksGrid::moveVertical(selectedIndex, total, pageItems, false));
  });
}

void ReaderRecentBooksActivity::loadVisiblePageMetadata() {
  if (books.empty()) return;
  const int pageItems =
      RecentBooksGrid::itemsPerPageForCount(static_cast<int>(books.size()), RecentBooksGrid::kQuickItemsPerPage);
  const int pageStart = (selectedIndex / pageItems) * pageItems;
  RecentBooksGrid::ensurePageProgress(books, pageStart, pageItems);
}

void ReaderRecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_RECENT_BOOKS));

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight + 30,
                      tr(STR_NO_OPEN_BOOK));
  } else {
    const int pageItems =
        RecentBooksGrid::itemsPerPageForCount(static_cast<int>(books.size()), RecentBooksGrid::kQuickItemsPerPage);
    const int currentPage = selectedIndex / pageItems;
    const int pageStart = currentPage * pageItems;
    const int pageCount = std::min(pageItems, static_cast<int>(books.size()) - pageStart);
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int metadataTop = contentTop;

    RecentBooksGrid::drawSelectedTitle(renderer, books, selectedIndex, metrics.contentSidePadding, metadataTop,
                                       pageWidth - metrics.contentSidePadding * 2, true);

    const int gridTop = contentTop + RecentBooksGrid::kTitleStripHeight + RecentBooksGrid::kTitleGridGap;
    const int dividerY = gridTop - RecentBooksGrid::kMetadataDividerGap;
    renderer.drawLine(metrics.contentSidePadding, dividerY, pageWidth - metrics.contentSidePadding, dividerY, true);
    const int dotsReserve = static_cast<int>(books.size()) > pageItems ? 24 : 0;
    const Rect gridRect{metrics.contentSidePadding, gridTop,
                        pageWidth - metrics.contentSidePadding * 2,
                        std::max(1, pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - dotsReserve -
                                        gridTop)};
    RecentBooksGrid::drawGridDynamic(renderer, books, selectedIndex, pageStart, pageCount, gridRect);

    const int totalPages = (static_cast<int>(books.size()) + pageItems - 1) / pageItems;
    if (totalPages > 1) {
      const int dotY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 10;
      RecentBooksGrid::drawPageDots(renderer, pageWidth, dotY, totalPages, currentPage);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();

  if (!books.empty()) {
    const int pageItems =
        RecentBooksGrid::itemsPerPageForCount(static_cast<int>(books.size()), RecentBooksGrid::kQuickItemsPerPage);
    const int pageStart = (selectedIndex / pageItems) * pageItems;
    if (pageStart != loadedPageStart) {
      const bool generated = RecentBooksGrid::loadPageCovers(renderer, books, pageStart, pageItems);
      loadedPageStart = pageStart;
      if (generated) {
        requestUpdate();
      }
    }
  }
}
