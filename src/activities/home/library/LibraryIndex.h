#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct LibraryIndexEntry {
  std::string path;
  std::string title;
  std::string author;
};

class LibraryIndexModel {
 public:
  static size_t countRealBooks(const std::vector<std::string>& entryPaths);
};
