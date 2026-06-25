#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;

struct LibraryCoverRecord {
  std::string bookPath;
  std::string sourceCoverPath;
  std::string thumbPath;
  int width = 0;
  int height = 0;
  uint8_t state = 0;
};

struct LibraryCoverWindowEntry {
  std::string bookPath;
  std::string thumbPath;
  uint8_t state = 0;
  int width = 0;
  int height = 0;
  int rowBytes = 0;
  std::unique_ptr<uint8_t[]> pixels;

  bool hasRenderedPixels() const { return width > 0 && height > 0 && rowBytes > 0 && pixels != nullptr; }
};

class LibraryCoverManager {
  std::vector<LibraryCoverWindowEntry> activeWindow;
  std::vector<LibraryCoverWindowEntry> recentWindow;
  std::vector<LibraryCoverWindowEntry> firstPageWindow;

 public:
  static void resetFailed(std::vector<LibraryCoverRecord>& records, uint8_t missingState, uint8_t unknownState);

  void setActiveWindow(std::vector<LibraryCoverWindowEntry> entries);
  void setFirstPageWindow(std::vector<LibraryCoverWindowEntry> entries);
  void cancelPrefetch();
  void clearWindows();
  bool isActiveBook(const std::string& bookPath) const;
  bool hasRendered(const std::string& bookPath, const std::string& thumbPath) const;
  bool drawRendered(GfxRenderer& renderer, const std::string& bookPath, const std::string& thumbPath, int x, int y,
                    int width, int height) const;
  bool loadRendered(const std::string& bookPath, const std::string& thumbPath);
  size_t activeSize() const { return activeWindow.size(); }
};
