#pragma once

#include <string>

class LibrarySearch {
 public:
  static std::string normalize(std::string value);
  static bool matches(const std::string& title, const std::string& author, const std::string& query);
};
