#pragma once

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

namespace RecentBooksGrid {
constexpr int kColumns = 3;
constexpr int kItemsPerPage = 9;
constexpr int kQuickItemsPerPage = 6;
constexpr int kCoverWidth = 123;
constexpr int kCoverHeight = 196;
constexpr int kGridSpacing = 8;
constexpr int kRowSpacing = 10;
constexpr int kTitleStripHeight = 42;
constexpr int kTitleGridGap = 20;
constexpr int kMetadataDividerGap = 12;
constexpr int kCoverCornerRadius = 2;
constexpr int kSelectionPadding = 4;
constexpr int kSelectionOutlineGap = 2;

struct Layout {
  int columns = kColumns;
  int rows = 3;
  int itemsPerPage = kItemsPerPage;
};

struct BookState {
  RecentBook book;
  std::string coverPath;
  std::string progressLabel;
  bool progressLoaded = false;
};

inline std::string titleFor(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const std::string name = slash == std::string::npos ? book.path : book.path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

inline std::string progressFor(const RecentBook& book) {
  const auto* stats = READING_STATS.findBook(!book.bookId.empty() ? book.bookId : book.path);
  return stats == nullptr ? "" : std::to_string(stats->lastProgressPercent) + "%";
}

inline void updateRecentBookCoverPath(const RecentBook& book, const std::string& coverBmpPath) {
  RECENT_BOOKS.updateBook(book.path, book.title, book.author, coverBmpPath, book.bookId);
}

inline bool hasThumbnailPlaceholder(const std::string& coverBmpPath) {
  return coverBmpPath.find("[WIDTH]") != std::string::npos || coverBmpPath.find("[HEIGHT]") != std::string::npos;
}

inline std::string getReusableCoverPath(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, "/.crosspoint").getThumbBmpPath();
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return Xtc(book.path, "/.crosspoint").getThumbBmpPath();
  }
  return book.coverBmpPath;
}

inline void ensureReusableCoverPath(RecentBook& book) {
  if (book.coverBmpPath.empty() || hasThumbnailPlaceholder(book.coverBmpPath)) {
    return;
  }

  const std::string reusablePath = getReusableCoverPath(book);
  if (reusablePath.empty() || reusablePath == book.coverBmpPath) {
    return;
  }

  book.coverBmpPath = reusablePath;
  updateRecentBookCoverPath(book, reusablePath);
}

inline bool needsCoverThumbGeneration(const RecentBook& book, const std::string& thumbPath) {
  if (thumbPath.empty() || !Storage.exists(thumbPath.c_str())) {
    return true;
  }
  if (!FsHelpers::hasXtcExtension(book.path)) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("RBG", thumbPath, file)) {
    return true;
  }
  Bitmap bitmap(file);
  const bool hasExpectedSize =
      bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == kCoverWidth && bitmap.getHeight() == kCoverHeight;
  file.close();
  return !hasExpectedSize;
}

inline int moveHorizontal(const int currentIndex, const int totalItems, const bool moveRight) {
  if (totalItems <= 0) return 0;
  return moveRight ? ButtonNavigator::nextIndex(currentIndex, totalItems)
                   : ButtonNavigator::previousIndex(currentIndex, totalItems);
}

inline Layout layoutForCount(const int totalItems, const int maxItemsPerPage = kItemsPerPage) {
  const int safeMax = std::max(1, maxItemsPerPage);
  const int visible = std::min(std::max(0, totalItems), safeMax);
  if (visible <= 1) return Layout{1, 1, 1};
  if (visible == 2) return Layout{2, 1, 2};
  if (visible == 3) return Layout{3, 1, 3};
  if (visible == 4) return Layout{2, 2, 4};
  if (visible <= 6) return Layout{3, 2, std::min(6, safeMax)};
  return Layout{3, 3, std::min(kItemsPerPage, safeMax)};
}

inline int itemsPerPageForCount(const int totalItems, const int maxItemsPerPage = kItemsPerPage) {
  return layoutForCount(totalItems, maxItemsPerPage).itemsPerPage;
}

inline int rowCountForLayout(const int pageCount, const int row) {
  if (pageCount <= 0 || row < 0) return 0;
  if (pageCount <= 3) return row == 0 ? pageCount : 0;
  if (pageCount == 4) return row < 2 ? 2 : 0;
  if (pageCount == 5) return row == 0 ? 3 : (row == 1 ? 2 : 0);
  if (pageCount <= 6) return row < 2 ? 3 : 0;
  if (row < 2) return 3;
  return pageCount - 6;
}

inline int rowStartForLayout(const int pageCount, const int row) {
  if (row <= 0) return 0;
  int start = 0;
  for (int currentRow = 0; currentRow < row; ++currentRow) {
    start += rowCountForLayout(pageCount, currentRow);
  }
  return start;
}

inline int rowsForLayout(const int pageCount) {
  int rows = 0;
  while (rowCountForLayout(pageCount, rows) > 0) {
    ++rows;
  }
  return std::max(1, rows);
}

inline int columnsForLayout(const int pageCount) {
  if (pageCount <= 1) return 1;
  if (pageCount == 2 || pageCount == 4) return 2;
  return 3;
}

inline int localRowForLayout(const int pageCount, const int localIndex) {
  int start = 0;
  const int rows = rowsForLayout(pageCount);
  for (int row = 0; row < rows; ++row) {
    const int count = rowCountForLayout(pageCount, row);
    if (localIndex < start + count) return row;
    start += count;
  }
  return std::max(0, rows - 1);
}

inline int localColumnForLayout(const int pageCount, const int localIndex) {
  const int row = localRowForLayout(pageCount, localIndex);
  return localIndex - rowStartForLayout(pageCount, row);
}

inline int localIndexForRowColumn(const int pageCount, const int row, const int column) {
  const int rows = rowsForLayout(pageCount);
  const int safeRow = std::max(0, std::min(row, rows - 1));
  const int count = rowCountForLayout(pageCount, safeRow);
  if (count <= 0) return 0;
  return rowStartForLayout(pageCount, safeRow) + std::max(0, std::min(column, count - 1));
}

inline int moveVertical(const int currentIndex, const int totalItems, const int itemsPerPage, const bool moveDown) {
  if (totalItems <= 0) return 0;
  const int safeItemsPerPage = std::max(1, itemsPerPage);
  const int totalPages = (totalItems + safeItemsPerPage - 1) / safeItemsPerPage;
  const int currentPage = currentIndex / safeItemsPerPage;
  const int indexInPage = currentIndex % safeItemsPerPage;
  const int currentPageStart = currentPage * safeItemsPerPage;
  const int currentPageCount = std::min(safeItemsPerPage, totalItems - currentPageStart);
  const int currentRow = localRowForLayout(currentPageCount, indexInPage);
  const int currentColumn = localColumnForLayout(currentPageCount, indexInPage);
  const int rowsPerPage = rowsForLayout(currentPageCount);

  if (moveDown) {
    if (currentRow < rowsPerPage - 1) {
      return currentPageStart + localIndexForRowColumn(currentPageCount, currentRow + 1, currentColumn);
    }

    const int nextPage = (currentPage + 1) % totalPages;
    const int nextPageStart = nextPage * safeItemsPerPage;
    const int nextPageCount = std::min(safeItemsPerPage, totalItems - nextPageStart);
    if (nextPageCount <= 0) return currentIndex;
    return nextPageStart + localIndexForRowColumn(nextPageCount, 0, currentColumn);
  }

  if (currentRow > 0) {
    return currentPageStart + localIndexForRowColumn(currentPageCount, currentRow - 1, currentColumn);
  }

  const int previousPage = (currentPage - 1 + totalPages) % totalPages;
  const int previousPageStart = previousPage * safeItemsPerPage;
  const int previousPageCount = std::min(safeItemsPerPage, totalItems - previousPageStart);
  if (previousPageCount <= 0) return currentIndex;

  return previousPageStart +
         localIndexForRowColumn(previousPageCount, rowsForLayout(previousPageCount) - 1, currentColumn);
}

inline void calculateCoverFillCrop(const Bitmap& bitmap, float& cropX, float& cropY) {
  cropX = 0.0f;
  cropY = 0.0f;
  const float srcW = static_cast<float>(bitmap.getWidth());
  const float srcH = static_cast<float>(bitmap.getHeight());
  if (srcW <= 0.0f || srcH <= 0.0f) return;

  const float srcRatio = srcW / srcH;
  const float targetRatio = static_cast<float>(kCoverWidth) / static_cast<float>(kCoverHeight);
  if (srcRatio > targetRatio) {
    cropX = std::max(0.0f, 1.0f - (targetRatio / srcRatio));
  } else if (srcRatio < targetRatio) {
    cropY = std::max(0.0f, 1.0f - (srcRatio / targetRatio));
  }
}

inline void drawPlaceholder(GfxRenderer& renderer, const Rect& rect) {
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kCoverCornerRadius, Color::White);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, kCoverCornerRadius, true);
  renderer.drawIcon(BookIcon, rect.x + (rect.width - 32) / 2, rect.y + (rect.height - 32) / 2, 32, 32);
}

inline void ensurePageProgress(std::vector<BookState>& books, const int pageStart, const int itemsPerPage) {
  if (pageStart < 0 || itemsPerPage <= 0) return;
  const int pageEnd = std::min(pageStart + itemsPerPage, static_cast<int>(books.size()));
  for (int index = pageStart; index < pageEnd; ++index) {
    BookState& state = books[index];
    if (!state.progressLoaded) {
      state.progressLabel = progressFor(state.book);
      state.progressLoaded = true;
    }
  }
}

inline bool loadPageCovers(GfxRenderer& renderer, std::vector<BookState>& books, const int pageStart,
                           const int itemsPerPage) {
  if (pageStart < 0 || itemsPerPage <= 0) return false;
  const int pageEnd = std::min(pageStart + itemsPerPage, static_cast<int>(books.size()));

  bool needsGeneration = false;
  for (int index = pageStart; index < pageEnd; ++index) {
    RecentBook& book = books[index].book;
    ensureReusableCoverPath(book);
    if (book.coverBmpPath.empty()) {
      books[index].coverPath = "";
      continue;
    }
    const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kCoverWidth, kCoverHeight);
    books[index].coverPath = (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) ? thumbPath : "";
    if (needsCoverThumbGeneration(book, thumbPath)) {
      needsGeneration = true;
    }
  }
  if (!needsGeneration) {
    return false;
  }

  bool showingLoading = false;
  Rect popupRect;
  const int totalToProcess = std::max(1, pageEnd - pageStart);
  int processedCount = 0;

  for (int index = pageStart; index < pageEnd; ++index) {
    RecentBook& book = books[index].book;
    const std::string coverPath =
        book.coverBmpPath.empty() ? "" : UITheme::getCoverThumbPath(book.coverBmpPath, kCoverWidth, kCoverHeight);
    if (needsCoverThumbGeneration(book, coverPath)) {
      if (FsHelpers::hasEpubExtension(book.path)) {
        Epub epub(book.path, "/.crosspoint");
        if (epub.load(false, true)) {
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + (processedCount * 90) / totalToProcess);
          if (epub.generateThumbBmp(kCoverWidth, kCoverHeight)) {
            const std::string reusablePath = epub.getThumbBmpPath();
            book.coverBmpPath = reusablePath;
            updateRecentBookCoverPath(book, reusablePath);
            books[index].coverPath = UITheme::getCoverThumbPath(reusablePath, kCoverWidth, kCoverHeight);
          } else {
            updateRecentBookCoverPath(book, "");
            book.coverBmpPath = "";
            books[index].coverPath = "";
          }
        }
      } else if (FsHelpers::hasXtcExtension(book.path)) {
        Xtc xtc(book.path, "/.crosspoint");
        if (xtc.load()) {
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + (processedCount * 90) / totalToProcess);
          if (xtc.generateThumbBmp(kCoverWidth, kCoverHeight)) {
            const std::string reusablePath = xtc.getThumbBmpPath();
            book.coverBmpPath = reusablePath;
            updateRecentBookCoverPath(book, reusablePath);
            books[index].coverPath = UITheme::getCoverThumbPath(reusablePath, kCoverWidth, kCoverHeight);
          } else {
            updateRecentBookCoverPath(book, "");
            book.coverBmpPath = "";
            books[index].coverPath = "";
          }
        }
      }
    }
    processedCount++;
  }

  return showingLoading;
}

inline std::string resolveExistingCoverPath(RecentBook& book) {
  ensureReusableCoverPath(book);
  if (book.coverBmpPath.empty()) {
    return "";
  }
  const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kCoverWidth, kCoverHeight);
  return (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) ? thumbPath : "";
}

inline std::string loadSingleCover(GfxRenderer& renderer, RecentBook& book) {
  std::vector<BookState> books;
  books.push_back(BookState{book});
  loadPageCovers(renderer, books, 0, 1);
  book = books.front().book;
  return books.front().coverPath;
}

inline void drawSelectedTitle(GfxRenderer& renderer, const std::vector<BookState>& books, const int selectedIndex,
                              const int x, const int y, const int width, const bool centered = false) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) return;
  const BookState& selected = books[selectedIndex];
  const std::string title =
      renderer.truncatedText(UI_10_FONT_ID, titleFor(selected.book).c_str(), width, EpdFontFamily::BOLD);
  const int titleW = renderer.getTextWidth(UI_10_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  const int titleX = centered ? x + std::max(0, (width - titleW) / 2) : x;
  renderer.drawText(UI_10_FONT_ID, titleX, y + 2, title.c_str(), true, EpdFontFamily::BOLD);

  std::string detailLine = selected.book.author;
  if (!selected.progressLabel.empty()) {
    if (!detailLine.empty()) detailLine += "  |  ";
    detailLine += selected.progressLabel;
  }
  if (!detailLine.empty()) {
    const std::string safeDetail = renderer.truncatedText(SMALL_FONT_ID, detailLine.c_str(), width);
    const int detailW = renderer.getTextWidth(SMALL_FONT_ID, safeDetail.c_str());
    const int detailX = centered ? x + std::max(0, (width - detailW) / 2) : x;
    renderer.drawText(SMALL_FONT_ID, detailX, y + 22, safeDetail.c_str(), true);
  }
}

inline void drawPageDots(GfxRenderer& renderer, const int pageWidth, const int y, const int totalPages,
                         const int currentPage) {
  if (totalPages <= 1) return;
  constexpr int dotSize = 8;
  constexpr int dotSpacing = 6;
  const int totalDotWidth = totalPages * dotSize + (totalPages - 1) * dotSpacing;
  int dotX = (pageWidth - totalDotWidth) / 2;
  for (int page = 0; page < totalPages; ++page) {
    if (page == currentPage) {
      renderer.fillRect(dotX, y, dotSize, dotSize, true);
    } else {
      renderer.drawRect(dotX, y, dotSize, dotSize, true);
    }
    dotX += dotSize + dotSpacing;
  }
}

inline void drawGrid(GfxRenderer& renderer, const std::vector<BookState>& books, const int selectedIndex,
                     const int pageStart, const int pageCount, const int startX, const int gridTop) {
  for (int index = 0; index < pageCount; ++index) {
    const int bookIndex = pageStart + index;
    const int col = index % kColumns;
    const int row = index / kColumns;
    const Rect cover{startX + col * (kCoverWidth + kGridSpacing), gridTop + row * (kCoverHeight + kRowSpacing),
                     kCoverWidth, kCoverHeight};
    bool drawn = false;
    std::string thumbPath = books[bookIndex].coverPath;
    if (thumbPath.empty() && !books[bookIndex].book.coverBmpPath.empty()) {
      thumbPath = UITheme::getCoverThumbPath(books[bookIndex].book.coverBmpPath, kCoverWidth, kCoverHeight);
    }
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      FsFile file;
      if (Storage.openFileForRead("RBG", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          float cropX = 0.0f;
          float cropY = 0.0f;
          calculateCoverFillCrop(bitmap, cropX, cropY);
          renderer.fillRoundedRect(cover.x, cover.y, cover.width, cover.height, kCoverCornerRadius, Color::White);
          renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height, cropX, cropY);
          renderer.maskRoundedRectOutsideCorners(cover.x, cover.y, cover.width, cover.height, kCoverCornerRadius,
                                                 Color::White);
          renderer.drawRoundedRect(cover.x, cover.y, cover.width, cover.height, 2, kCoverCornerRadius, true);
          drawn = true;
        }
        file.close();
      }
    }
    if (!drawn) drawPlaceholder(renderer, cover);
    if (bookIndex == selectedIndex) {
      const int selectionOuterInset = kSelectionPadding + kSelectionOutlineGap;
      renderer.drawRoundedRect(cover.x - kSelectionPadding, cover.y - kSelectionPadding,
                               cover.width + kSelectionPadding * 2, cover.height + kSelectionPadding * 2, 3,
                               kCoverCornerRadius + kSelectionPadding, true);
      renderer.drawRoundedRect(cover.x - selectionOuterInset, cover.y - selectionOuterInset,
                               cover.width + selectionOuterInset * 2, cover.height + selectionOuterInset * 2, 1,
                               kCoverCornerRadius + selectionOuterInset, true);
    }
  }
}

inline Rect dynamicCoverRect(const Rect& gridRect, const int pageCount, const int localIndex) {
  const int columns = columnsForLayout(pageCount);
  const int rows = rowsForLayout(pageCount);
  const int maxCoverWidth = std::max(1, (gridRect.width - (columns - 1) * kGridSpacing) / columns);
  const int maxCoverHeight = std::max(1, (gridRect.height - (rows - 1) * kRowSpacing) / rows);
  int coverWidth = maxCoverWidth;
  int coverHeight = (coverWidth * kCoverHeight) / kCoverWidth;
  if (coverHeight > maxCoverHeight) {
    coverHeight = maxCoverHeight;
    coverWidth = (coverHeight * kCoverWidth) / kCoverHeight;
  }
  coverWidth = std::max(1, coverWidth);
  coverHeight = std::max(1, coverHeight);

  const int row = localRowForLayout(pageCount, localIndex);
  const int column = localColumnForLayout(pageCount, localIndex);
  const int rowCount = rowCountForLayout(pageCount, row);
  const int rowWidth = rowCount * coverWidth + std::max(0, rowCount - 1) * kGridSpacing;
  const int gridHeight = rows * coverHeight + std::max(0, rows - 1) * kRowSpacing;
  const int x = gridRect.x + std::max(0, (gridRect.width - rowWidth) / 2) + column * (coverWidth + kGridSpacing);
  const int y = gridRect.y + std::max(0, (gridRect.height - gridHeight) / 2) + row * (coverHeight + kRowSpacing);
  return Rect{x, y, coverWidth, coverHeight};
}

inline void drawGridDynamic(GfxRenderer& renderer, const std::vector<BookState>& books, const int selectedIndex,
                            const int pageStart, const int pageCount, const Rect& gridRect) {
  for (int index = 0; index < pageCount; ++index) {
    const int bookIndex = pageStart + index;
    const Rect cover = dynamicCoverRect(gridRect, pageCount, index);
    bool drawn = false;
    std::string thumbPath = books[bookIndex].coverPath;
    if (thumbPath.empty() && !books[bookIndex].book.coverBmpPath.empty()) {
      thumbPath = UITheme::getCoverThumbPath(books[bookIndex].book.coverBmpPath, kCoverWidth, kCoverHeight);
    }
    if (!thumbPath.empty() && Storage.exists(thumbPath.c_str())) {
      FsFile file;
      if (Storage.openFileForRead("RBG", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          float cropX = 0.0f;
          float cropY = 0.0f;
          calculateCoverFillCrop(bitmap, cropX, cropY);
          renderer.fillRoundedRect(cover.x, cover.y, cover.width, cover.height, kCoverCornerRadius, Color::White);
          renderer.drawBitmap(bitmap, cover.x, cover.y, cover.width, cover.height, cropX, cropY);
          renderer.maskRoundedRectOutsideCorners(cover.x, cover.y, cover.width, cover.height, kCoverCornerRadius,
                                                 Color::White);
          renderer.drawRoundedRect(cover.x, cover.y, cover.width, cover.height, 2, kCoverCornerRadius, true);
          drawn = true;
        }
        file.close();
      }
    }
    if (!drawn) drawPlaceholder(renderer, cover);
    if (bookIndex == selectedIndex) {
      const int selectionOuterInset = kSelectionPadding + kSelectionOutlineGap;
      renderer.drawRoundedRect(cover.x - kSelectionPadding, cover.y - kSelectionPadding,
                               cover.width + kSelectionPadding * 2, cover.height + kSelectionPadding * 2, 3,
                               kCoverCornerRadius + kSelectionPadding, true);
      renderer.drawRoundedRect(cover.x - selectionOuterInset, cover.y - selectionOuterInset,
                               cover.width + selectionOuterInset * 2, cover.height + selectionOuterInset * 2, 1,
                               kCoverCornerRadius + selectionOuterInset, true);
    }
  }
}

}  // namespace RecentBooksGrid
