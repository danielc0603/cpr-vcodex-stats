#include "LibrarySearch.h"

#include <algorithm>
#include <cctype>

std::string LibrarySearch::normalize(std::string value) {
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
                return !std::isspace(c);
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
                return !std::isspace(c);
              }).base(),
              value.end());
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool LibrarySearch::matches(const std::string& title, const std::string& author, const std::string& query) {
  const std::string needle = normalize(query);
  if (needle.empty()) {
    return true;
  }
  return normalize(title).find(needle) != std::string::npos ||
         normalize(author).find(needle) != std::string::npos;
}
