#include "LibraryIndex.h"

size_t LibraryIndexModel::countRealBooks(const std::vector<std::string>& entryPaths) {
  size_t count = 0;
  for (const auto& path : entryPaths) {
    if (!path.empty()) {
      ++count;
    }
  }
  return count;
}
