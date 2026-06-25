#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct LibraryState {
  uint8_t filter = 0;
  uint8_t sort = 0;
  bool sortDescending = false;
  uint8_t layout = 0;
  size_t selection = 0;
  std::string authorKey;
  std::string authorName;
  std::string searchQuery;

  std::string token() const;
};
