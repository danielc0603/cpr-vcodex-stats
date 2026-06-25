#include "LibraryCoverManager.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <new>
#include <utility>

namespace {
constexpr int kLibraryCoverCornerRadius = 2;
constexpr int kMaxLibraryRenderedCoverWidth = 360;
constexpr int kMaxLibraryRenderedCoverHeight = 540;
constexpr size_t kMaxLibraryRenderedCoverBytes = 49152;
constexpr uint32_t kMinHeapAfterCoverDecode = 36000;

void moveRenderedPixels(LibraryCoverWindowEntry& target, LibraryCoverWindowEntry& source) {
  target.width = source.width;
  target.height = source.height;
  target.rowBytes = source.rowBytes;
  target.pixels = std::move(source.pixels);
}

void reuseRenderedPixels(std::vector<LibraryCoverWindowEntry>& entries,
                         std::vector<LibraryCoverWindowEntry>& sourceWindow) {
  for (auto& entry : entries) {
    if (entry.hasRenderedPixels()) continue;
    auto previous = std::find_if(sourceWindow.begin(), sourceWindow.end(), [&entry](const auto& oldEntry) {
      return oldEntry.bookPath == entry.bookPath && oldEntry.thumbPath == entry.thumbPath;
    });
    if (previous != sourceWindow.end()) {
      moveRenderedPixels(entry, *previous);
    }
  }
}
}

void LibraryCoverManager::resetFailed(std::vector<LibraryCoverRecord>& records, const uint8_t missingState,
                                      const uint8_t unknownState) {
  for (auto& record : records) {
    if (record.state == missingState) {
      record.state = unknownState;
      record.thumbPath.clear();
    }
  }
}

void LibraryCoverManager::setActiveWindow(std::vector<LibraryCoverWindowEntry> entries) {
  reuseRenderedPixels(entries, activeWindow);
  reuseRenderedPixels(entries, recentWindow);
  reuseRenderedPixels(entries, firstPageWindow);
  recentWindow = std::move(activeWindow);
  activeWindow = std::move(entries);
  recentWindow.erase(std::remove_if(recentWindow.begin(), recentWindow.end(),
                                    [](const auto& entry) { return !entry.hasRenderedPixels(); }),
                     recentWindow.end());
}

void LibraryCoverManager::setFirstPageWindow(std::vector<LibraryCoverWindowEntry> entries) {
  reuseRenderedPixels(entries, firstPageWindow);
  reuseRenderedPixels(entries, activeWindow);
  reuseRenderedPixels(entries, recentWindow);
  firstPageWindow = std::move(entries);
  firstPageWindow.erase(std::remove_if(firstPageWindow.begin(), firstPageWindow.end(),
                                       [](const auto& entry) { return !entry.hasRenderedPixels(); }),
                        firstPageWindow.end());
}

void LibraryCoverManager::cancelPrefetch() {}

void LibraryCoverManager::clearWindows() {
  activeWindow.clear();
  recentWindow.clear();
  firstPageWindow.clear();
}

bool LibraryCoverManager::isActiveBook(const std::string& bookPath) const {
  return std::any_of(activeWindow.begin(), activeWindow.end(),
                     [&bookPath](const auto& entry) { return entry.bookPath == bookPath; });
}

bool LibraryCoverManager::hasRendered(const std::string& bookPath, const std::string& thumbPath) const {
  if (bookPath.empty() || thumbPath.empty()) return false;
  const auto matches = [&](const auto& entry) {
    return entry.bookPath == bookPath && entry.thumbPath == thumbPath && entry.hasRenderedPixels();
  };
  return std::any_of(activeWindow.begin(), activeWindow.end(), matches) ||
         std::any_of(recentWindow.begin(), recentWindow.end(), matches) ||
         std::any_of(firstPageWindow.begin(), firstPageWindow.end(), matches);
}

bool LibraryCoverManager::drawRendered(GfxRenderer& renderer, const std::string& bookPath,
                                       const std::string& thumbPath, const int x, const int y, const int width,
                                       const int height) const {
  if (bookPath.empty() || thumbPath.empty() || x < 0 || y < 0 || width <= 0 || height <= 0) return false;
  const LibraryCoverWindowEntry* rendered = nullptr;
  auto entry = std::find_if(activeWindow.begin(), activeWindow.end(), [&](const auto& item) {
    return item.bookPath == bookPath && item.thumbPath == thumbPath && item.hasRenderedPixels();
  });
  if (entry != activeWindow.end()) {
    rendered = &*entry;
  } else {
    entry = std::find_if(firstPageWindow.begin(), firstPageWindow.end(), [&](const auto& item) {
      return item.bookPath == bookPath && item.thumbPath == thumbPath && item.hasRenderedPixels();
    });
    if (entry != firstPageWindow.end()) rendered = &*entry;
  }
  if (rendered == nullptr) return false;
  const size_t entryBytes = static_cast<size_t>(rendered->rowBytes) * static_cast<size_t>(rendered->height);
  if (rendered->rowBytes <= 0 || entryBytes == 0 || entryBytes > kMaxLibraryRenderedCoverBytes) {
    return false;
  }
  renderer.fillRoundedRect(x, y, width, height, kLibraryCoverCornerRadius, Color::White);
  for (int dy = 0; dy < height; ++dy) {
    const int sy = dy * rendered->height / height;
    const uint8_t* srcRow = rendered->pixels.get() + sy * rendered->rowBytes;
    for (int dx = 0; dx < width; ++dx) {
      const int sx = dx * rendered->width / width;
      const uint8_t value = (srcRow[sx / 4] >> (6 - ((sx % 4) * 2))) & 0x03;
      renderer.drawPixelDirect(x + dx, y + dy, value < 3);
    }
  }
  renderer.maskRoundedRectOutsideCorners(x, y, width, height, kLibraryCoverCornerRadius, Color::White);
  renderer.drawRoundedRect(x, y, width, height, 2, kLibraryCoverCornerRadius, true);
  return true;
}

bool LibraryCoverManager::loadRendered(const std::string& bookPath, const std::string& thumbPath) {
  if (bookPath.empty() || thumbPath.empty()) return false;
  auto entry = std::find_if(activeWindow.begin(), activeWindow.end(), [&](const auto& item) {
    return item.bookPath == bookPath && item.thumbPath == thumbPath;
  });
  if (entry == activeWindow.end()) return false;
  if (entry->hasRenderedPixels()) return true;

  FsFile file;
  if (!Storage.openFileForRead("LCM", thumbPath, file)) return false;
  Bitmap bitmap(file, false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    file.close();
    return false;
  }
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  const int rowBytes = (width + 3) / 4;
  const int sourceRowBytes = bitmap.getRowBytes();
  if (width <= 0 || height <= 0 || rowBytes <= 0 || sourceRowBytes <= 0 ||
      width > kMaxLibraryRenderedCoverWidth || height > kMaxLibraryRenderedCoverHeight) {
    file.close();
    return false;
  }
  const size_t pixelBytes = static_cast<size_t>(rowBytes) * static_cast<size_t>(height);
  const size_t scratchBytes = static_cast<size_t>(sourceRowBytes);
  if (pixelBytes == 0 || scratchBytes == 0 || pixelBytes > kMaxLibraryRenderedCoverBytes ||
      ESP.getFreeHeap() < pixelBytes + scratchBytes + kMinHeapAfterCoverDecode) {
    file.close();
    return false;
  }

  std::unique_ptr<uint8_t[]> pixels(new (std::nothrow) uint8_t[pixelBytes]);
  std::unique_ptr<uint8_t[]> rowScratch(new (std::nothrow) uint8_t[scratchBytes]);
  if (!pixels || !rowScratch) {
    file.close();
    return false;
  }
  for (int row = 0; row < height; ++row) {
    if (bitmap.readNextRow(pixels.get() + static_cast<size_t>(row) * rowBytes, rowScratch.get()) !=
        BmpReaderError::Ok) {
      file.close();
      return false;
    }
  }
  file.close();
  entry->width = width;
  entry->height = height;
  entry->rowBytes = rowBytes;
  entry->pixels = std::move(pixels);
  return true;
}
