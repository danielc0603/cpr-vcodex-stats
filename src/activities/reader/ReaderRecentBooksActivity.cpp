#include "ReaderRecentBooksActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
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
  RECENT_BOOKS.repairOrRemoveMissingBooks();
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
  const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
  const int pageItems = gridView ? RecentBooksGrid::itemsPerPageForCount(total, RecentBooksGrid::kQuickItemsPerPage)
                                 : UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
  auto moveSelection = [this, total](const int next) {
    selectedIndex = next;
    loadVisiblePageMetadata();
    requestUpdate();
  };
  if (!gridView) {
    buttonNavigator.onNextPress([this, total, moveSelection] {
      moveSelection(ButtonNavigator::nextIndex(selectedIndex, total));
    });
    buttonNavigator.onPreviousPress([this, total, moveSelection] {
      moveSelection(ButtonNavigator::previousIndex(selectedIndex, total));
    });
    buttonNavigator.onNextContinuous([this, total, pageItems, moveSelection] {
      moveSelection(ButtonNavigator::nextPageIndex(selectedIndex, total, pageItems));
    });
    buttonNavigator.onPreviousContinuous([this, total, pageItems, moveSelection] {
      moveSelection(ButtonNavigator::previousPageIndex(selectedIndex, total, pageItems));
    });
    return;
  }
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
  const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
  const int pageItems = gridView
                            ? RecentBooksGrid::itemsPerPageForCount(static_cast<int>(books.size()),
                                                                    RecentBooksGrid::kQuickItemsPerPage)
                            : UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
  const int pageStart = (selectedIndex / pageItems) * pageItems;
  RecentBooksGrid::ensurePageProgress(books, pageStart, pageItems);
}

void ReaderRecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int panelW = pageWidth - metrics.contentSidePadding * 2;
  const int panelH = pageHeight - metrics.topPadding * 2 - metrics.buttonHintsHeight - 8;
  const int panelX = metrics.contentSidePadding;
  const int panelY = metrics.topPadding + 6;
  const Rect panel{panelX, panelY, panelW, panelH};
  renderer.fillRoundedRect(panel.x, panel.y, panel.width, panel.height, 8, Color::White);
  renderer.drawRoundedRect(panel.x, panel.y, panel.width, panel.height, 1, 8, true);
  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_RECENT_BOOKS), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, panel.x + std::max(0, (panel.width - titleW) / 2), panel.y + 13,
                    tr(STR_RECENT_BOOKS), true, EpdFontFamily::BOLD);
  const int titleDividerY = panel.y + 42;
  renderer.drawLine(panel.x + 14, titleDividerY, panel.x + panel.width - 14, titleDividerY, true);

  if (books.empty()) {
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, tr(STR_NO_OPEN_BOOK));
    renderer.drawText(UI_10_FONT_ID, panel.x + std::max(0, (panel.width - textW) / 2), titleDividerY + 24,
                      tr(STR_NO_OPEN_BOOK));
  } else {
    const bool gridView = SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID;
    const int pageItems = gridView
                              ? RecentBooksGrid::itemsPerPageForCount(static_cast<int>(books.size()),
                                                                      RecentBooksGrid::kQuickItemsPerPage)
                              : UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);
    const int currentPage = selectedIndex / pageItems;
    const int pageStart = currentPage * pageItems;
    const int pageCount = std::min(pageItems, static_cast<int>(books.size()) - pageStart);
    const int contentTop = titleDividerY + metrics.verticalSpacing;
    if (!gridView) {
      GUI.drawList(renderer, Rect{panel.x + 8, contentTop, panel.width - 16,
                                  panel.y + panel.height - contentTop - metrics.verticalSpacing},
                   books.size(), selectedIndex,
                   [this](int index) { return RecentBooksGrid::titleFor(books[index].book); },
                   [this](int index) {
                     return books[index].book.author.empty() ? books[index].progressLabel : books[index].book.author;
                   },
                   [this](int index) { return UITheme::getFileIcon(books[index].book.path); }, nullptr, false);
    } else {
      const int metadataTop = contentTop;

      RecentBooksGrid::drawSelectedTitle(renderer, books, selectedIndex, panel.x + 14, metadataTop,
                                         panel.width - 28, true);

      const int gridTop = contentTop + RecentBooksGrid::kTitleStripHeight + RecentBooksGrid::kTitleGridGap;
      const int dividerY = gridTop - RecentBooksGrid::kMetadataDividerGap;
      renderer.drawLine(panel.x + 14, dividerY, panel.x + panel.width - 14, dividerY, true);
      const int dotsReserve = static_cast<int>(books.size()) > pageItems ? 24 : 0;
      const Rect gridRect{panel.x + 14, gridTop, panel.width - 28,
                          std::max(1, panel.y + panel.height - metrics.verticalSpacing - dotsReserve - gridTop)};
      RecentBooksGrid::drawGridDynamic(renderer, books, selectedIndex, pageStart, pageCount, gridRect);

      const int totalPages = (static_cast<int>(books.size()) + pageItems - 1) / pageItems;
      if (totalPages > 1) {
        const int dotY = panel.y + panel.height - 18;
        RecentBooksGrid::drawPageDots(renderer, pageWidth, dotY, totalPages, currentPage);
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  if (!books.empty() && SETTINGS.recentBooksView == CrossPointSettings::RECENT_BOOKS_GRID) {
    const int pageItems =
        RecentBooksGrid::itemsPerPageForCount(static_cast<int>(books.size()), RecentBooksGrid::kQuickItemsPerPage);
    const int pageStart = (selectedIndex / pageItems) * pageItems;
    if (pageStart != loadedPageStart) {
      const int pageCount = std::min(pageItems, static_cast<int>(books.size()) - pageStart);
      const int contentTop = titleDividerY + metrics.verticalSpacing;
      const int gridTop = contentTop + RecentBooksGrid::kTitleStripHeight + RecentBooksGrid::kTitleGridGap;
      const int dotsReserve = static_cast<int>(books.size()) > pageItems ? 24 : 0;
      const Rect gridRect{panel.x + 14, gridTop, panel.width - 28,
                          std::max(1, panel.y + panel.height - metrics.verticalSpacing - dotsReserve - gridTop)};
      const Rect coverRect = RecentBooksGrid::dynamicCoverRect(gridRect, pageCount, 0);
      const bool generated =
          RecentBooksGrid::loadPageCovers(renderer, books, pageStart, pageItems, coverRect.width, coverRect.height);
      loadedPageStart = pageStart;
      if (generated) {
        requestUpdate();
      }
    }
  }
}
