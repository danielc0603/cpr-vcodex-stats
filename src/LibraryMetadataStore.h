#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CachedBookMetadata;

struct LibraryBookMetadata {
  std::string stableId;
  std::string bookId;
  std::string epubIdentifier;
  std::string path;
  std::vector<std::string> knownPaths;
  std::string filename;
  std::string extension;
  std::string title;
  std::string subtitle;
  std::string author;
  std::string sortTitle;
  std::string sortAuthor;
  std::string language;
  std::string publisher;
  std::string publishedDate;
  std::string edition;
  std::string copyright;
  std::string seriesName;
  std::string seriesNumber;
  std::string seriesIndex;
  std::string seriesTotal;
  std::string isbn;
  std::string asin;
  std::string doi;
  std::string calibreId;
  std::string uuid;
  std::string genre;
  std::string subjects;
  std::string tags;
  std::string keywords;
  std::string description;
  std::string shortDescription;
  std::string blurb;
  std::string coverPath;
  std::string coverHash;
  uint16_t coverWidth = 0;
  uint16_t coverHeight = 0;
  uint32_t coverLastGenerated = 0;
  std::string format;
  uint32_t fileSize = 0;
  uint32_t pageCount = 0;
  uint32_t chapterCount = 0;
  uint32_t wordCount = 0;
  uint32_t estimatedReadingTime = 0;
  std::string metadataSource;
  uint16_t metadataVersion = 0;
  uint32_t lastMetadataScan = 0;
  uint32_t lastMetadataUpdate = 0;
  bool pinned = false;
  bool toRead = false;
  bool finished = false;
  bool activeRemoved = false;
  uint32_t updatedAt = 0;
};

class LibraryMetadataStore {
  static LibraryMetadataStore instance;

  std::vector<LibraryBookMetadata> books;

  int findBookIndex(const std::string& path, const std::string& bookId) const;
  LibraryBookMetadata& getOrCreateBook(const std::string& path, const std::string& bookId = "");
  void normalizeBook(LibraryBookMetadata& book);
  void normalizeBooks();
  void rememberKnownPath(LibraryBookMetadata& book, const std::string& path);

 public:
  ~LibraryMetadataStore() = default;

  static LibraryMetadataStore& getInstance() { return instance; }

  const LibraryBookMetadata* findBook(const std::string& key) const;
  bool isPinned(const std::string& key) const;
  bool isToRead(const std::string& key) const;
  bool isFinished(const std::string& key) const;
  bool isActiveRemoved(const std::string& key) const;
  uint8_t cycleShelfState(const std::string& path, const std::string& bookId = "");
  void setToRead(const std::string& path, const std::string& bookId = "");
  void setFinished(const std::string& path, const std::string& bookId = "");
  void removeFromToRead(const std::string& path, const std::string& bookId = "");
  void removeFinishedState(const std::string& path, const std::string& bookId = "");
  void removeActiveReadingState(const std::string& path, const std::string& bookId = "");
  void clearReadingState(const std::string& path, const std::string& bookId = "");
  void rememberBook(const std::string& path, const std::string& bookId = "");
  bool upsertBookMetadata(const std::string& path, const std::string& bookId, const CachedBookMetadata& metadata,
                          const std::string& source, bool persist = true);
  bool repairRenamedBooks(const std::vector<std::string>& livePaths);

  const std::vector<LibraryBookMetadata>& getBooks() const { return books; }

  bool saveToFile() const;
  bool loadFromFile();
};

#define LIBRARY_METADATA LibraryMetadataStore::getInstance()
