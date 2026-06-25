#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class LibraryCollectionView : uint8_t {
  None = 0,
  Series = 1,
  Author = 2,
  Manual = 3,
};

struct LibraryManualSeriesBook {
  std::string stableBookId;
  std::string path;
  uint16_t order = 0;
};

struct LibraryManualSeries {
  std::string id;
  std::string name;
  std::vector<LibraryManualSeriesBook> books;
};

class LibraryManualSeriesStore {
 public:
  static constexpr const char* kPath = "/.crosspoint/library_manual_series.tsv";

  const std::vector<LibraryManualSeries>& series() const { return series_; }
  void clear() { series_.clear(); }
  bool load();
  bool save() const;
  bool assignBook(const std::string& seriesName, const std::string& stableBookId, const std::string& path);
  bool removeBook(const std::string& stableBookId, const std::string& path);
  std::vector<std::string> names() const;
  std::vector<LibraryManualSeriesBook> booksForSeries(const std::string& seriesName) const;

 private:
  std::vector<LibraryManualSeries> series_;
};
