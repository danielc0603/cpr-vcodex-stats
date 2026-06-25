#include "LibraryState.h"

std::string LibraryState::token() const {
  return std::to_string(filter) + "|" + std::to_string(sort) + "|" +
         (sortDescending ? "1" : "0") + "|" + std::to_string(layout) + "|" +
         std::to_string(selection) + "|" + authorKey + "|" + searchQuery;
}
