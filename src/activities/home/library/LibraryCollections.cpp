#include "LibraryCollections.h"

#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <sstream>

namespace {
std::string trimSeriesValue(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  value.erase(std::remove(value.begin(), value.end(), '\t'), value.end());
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
  value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
  return value;
}

std::string makeSeriesId(const std::string& name) {
  std::string id = trimSeriesValue(name);
  std::transform(id.begin(), id.end(), id.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  for (auto& ch : id) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      ch = '_';
    }
  }
  return id;
}
}  // namespace

bool LibraryManualSeriesStore::load() {
  series_.clear();
  if (!Storage.exists(kPath)) {
    return true;
  }
  const String raw = Storage.readFile(kPath);
  std::stringstream stream(raw.c_str());
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) continue;
    std::vector<std::string> fields;
    std::stringstream row(line);
    std::string field;
    while (std::getline(row, field, '\t')) {
      fields.push_back(field);
    }
    if (fields.size() < 5 || fields[0] != "book") continue;
    const std::string id = trimSeriesValue(fields[1]);
    const std::string name = trimSeriesValue(fields[2]);
    const std::string stableBookId = trimSeriesValue(fields[3]);
    const std::string path = trimSeriesValue(fields[4]);
    if (id.empty() || name.empty() || (stableBookId.empty() && path.empty())) continue;
    auto series = std::find_if(series_.begin(), series_.end(), [&id](const auto& item) { return item.id == id; });
    if (series == series_.end()) {
      series_.push_back(LibraryManualSeries{id, name, {}});
      series = std::prev(series_.end());
    }
    LibraryManualSeriesBook book;
    book.stableBookId = stableBookId;
    book.path = path;
    if (fields.size() > 5) {
      book.order = static_cast<uint16_t>(std::max(0, atoi(fields[5].c_str())));
    } else {
      book.order = static_cast<uint16_t>(series->books.size() + 1);
    }
    series->books.push_back(std::move(book));
  }
  return true;
}

bool LibraryManualSeriesStore::save() const {
  Storage.mkdir("/.crosspoint");
  std::string output;
  for (const auto& series : series_) {
    for (const auto& book : series.books) {
      output += "book\t" + series.id + "\t" + series.name + "\t" + book.stableBookId + "\t" + book.path + "\t" +
                std::to_string(book.order) + "\n";
    }
  }
  return Storage.writeFile(kPath, String(output.c_str()));
}

bool LibraryManualSeriesStore::assignBook(const std::string& seriesName, const std::string& stableBookId,
                                          const std::string& path) {
  const std::string name = trimSeriesValue(seriesName);
  if (name.empty() || (stableBookId.empty() && path.empty())) return false;
  const std::string id = makeSeriesId(name);
  auto series = std::find_if(series_.begin(), series_.end(), [&id](const auto& item) { return item.id == id; });
  if (series == series_.end()) {
    series_.push_back(LibraryManualSeries{id, name, {}});
    series = std::prev(series_.end());
  }
  auto existing = std::find_if(series->books.begin(), series->books.end(), [&](const auto& book) {
    return (!stableBookId.empty() && book.stableBookId == stableBookId) || (!path.empty() && book.path == path);
  });
  if (existing != series->books.end()) {
    existing->stableBookId = stableBookId;
    existing->path = path;
    return save();
  }
  LibraryManualSeriesBook book;
  book.stableBookId = stableBookId;
  book.path = path;
  book.order = static_cast<uint16_t>(series->books.size() + 1);
  series->books.push_back(std::move(book));
  return save();
}

bool LibraryManualSeriesStore::removeBook(const std::string& stableBookId, const std::string& path) {
  if (stableBookId.empty() && path.empty()) return false;
  bool changed = false;
  for (auto& series : series_) {
    const auto before = series.books.size();
    series.books.erase(std::remove_if(series.books.begin(), series.books.end(), [&](const auto& book) {
                         return (!stableBookId.empty() && book.stableBookId == stableBookId) ||
                                (!path.empty() && book.path == path);
                       }),
                       series.books.end());
    if (series.books.size() != before) {
      changed = true;
      for (size_t index = 0; index < series.books.size(); ++index) {
        series.books[index].order = static_cast<uint16_t>(index + 1);
      }
    }
  }
  series_.erase(std::remove_if(series_.begin(), series_.end(), [](const auto& series) { return series.books.empty(); }),
                series_.end());
  return changed ? save() : true;
}

std::vector<std::string> LibraryManualSeriesStore::names() const {
  std::vector<std::string> result;
  result.reserve(series_.size());
  for (const auto& series : series_) {
    result.push_back(series.name);
  }
  return result;
}

std::vector<LibraryManualSeriesBook> LibraryManualSeriesStore::booksForSeries(const std::string& seriesName) const {
  const std::string id = makeSeriesId(seriesName);
  auto series = std::find_if(series_.begin(), series_.end(), [&id](const auto& item) { return item.id == id; });
  if (series == series_.end()) return {};
  std::vector<LibraryManualSeriesBook> books = series->books;
  std::sort(books.begin(), books.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
  return books;
}
