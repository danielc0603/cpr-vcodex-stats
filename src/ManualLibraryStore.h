#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ManualLibraryBook {
  std::string stableId;
  std::string path;
  std::string manualTitle;
  std::string manualAuthor;
  std::string manualSeries;
  std::string manualSeriesNumber;
  std::vector<std::string> collections;
  std::vector<std::string> personalTags;
  std::string customCover;
  std::string customThumbnail;
  std::string notes;
  uint8_t rating = 0;
  bool favorite = false;
  bool pinned = false;
  bool hidden = false;
  bool archived = false;
  bool ignoreMetadataUpdates = false;
  uint32_t updatedAt = 0;
};

class ManualLibraryStore {
  static ManualLibraryStore instance;

  std::vector<ManualLibraryBook> books;

  int findBookIndex(const std::string& path, const std::string& stableId) const;
  ManualLibraryBook& getOrCreateBook(const std::string& path, const std::string& stableId = "");
  void normalizeBook(ManualLibraryBook& book);
  void normalizeBooks();

 public:
  static ManualLibraryStore& getInstance() { return instance; }

  const ManualLibraryBook* findBook(const std::string& path, const std::string& stableId = "") const;
  bool setManualTitle(const std::string& path, const std::string& title, const std::string& stableId = "");
  bool setManualAuthor(const std::string& path, const std::string& author, const std::string& stableId = "");
  bool setManualSeries(const std::string& path, const std::string& series, const std::string& number = "",
                       const std::string& stableId = "");
  bool setPersonalTags(const std::string& path, const std::string& tags, const std::string& stableId = "");
  bool setNotes(const std::string& path, const std::string& notes, const std::string& stableId = "");
  bool setRating(const std::string& path, uint8_t rating, const std::string& stableId = "");
  bool setCustomCover(const std::string& path, const std::string& coverPath, const std::string& stableId = "");
  bool setFavorite(const std::string& path, bool favorite, const std::string& stableId = "");
  bool setHidden(const std::string& path, bool hidden, const std::string& stableId = "");
  bool setArchived(const std::string& path, bool archived, const std::string& stableId = "");
  bool setIgnoreMetadataUpdates(const std::string& path, bool ignore, const std::string& stableId = "");

  const std::vector<ManualLibraryBook>& getBooks() const { return books; }

  bool saveToFile() const;
  bool loadFromFile();
};

#define MANUAL_LIBRARY ManualLibraryStore::getInstance()
