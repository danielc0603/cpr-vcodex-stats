#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>

#include "LibraryMetadataStore.h"
#include "ReadingStatsStore.h"
#include "util/BookIdentity.h"

namespace {
constexpr uint8_t RECENT_BOOKS_FILE_VERSION = 3;
constexpr char RECENT_BOOKS_FILE_BIN[] = "/.crosspoint/recent.bin";
constexpr char RECENT_BOOKS_FILE_JSON[] = "/.crosspoint/recent.json";
constexpr char RECENT_BOOKS_FILE_BAK[] = "/.crosspoint/recent.bin.bak";
constexpr int MAX_RECENT_BOOKS = 10;

std::string fallbackTitleFromPath(const std::string& path) {
  std::string filename = path;
  const size_t lastSlash = filename.find_last_of('/');
  if (lastSlash != std::string::npos) {
    filename = filename.substr(lastSlash + 1);
  }

  const size_t dotPos = filename.find_last_of('.');
  if (dotPos != std::string::npos) {
    filename = filename.substr(0, dotPos);
  }
  return filename;
}
}  // namespace

RecentBooksStore RecentBooksStore::instance;

int RecentBooksStore::findBookIndex(const std::string& path, const std::string& bookId) const {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  for (int index = 0; index < static_cast<int>(recentBooks.size()); ++index) {
    const auto& book = recentBooks[index];
    if (!bookId.empty() && !book.bookId.empty() && book.bookId == bookId) {
      return index;
    }
    if (!normalizedPath.empty() && book.path == normalizedPath) {
      return index;
    }
  }
  return -1;
}

void RecentBooksStore::normalizeBook(RecentBook& book) {
  book.path = BookIdentity::normalizePath(book.path);
  if (!book.bookId.empty()) {
    return;
  }

  if (!book.path.empty() && Storage.exists(book.path.c_str())) {
    book.bookId = BookIdentity::resolveStableBookId(book.path);
    return;
  }

  if (const auto* statsBook = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author)) {
    book.bookId = statsBook->bookId;
  }
}

void RecentBooksStore::normalizeBooks() {
  for (auto& book : recentBooks) {
    normalizeBook(book);
  }

  std::vector<RecentBook> normalized;
  normalized.reserve(recentBooks.size());
  for (const auto& book : recentBooks) {
    const int existingIndex = [&normalized, &book]() {
      for (int index = 0; index < static_cast<int>(normalized.size()); ++index) {
        const auto& existing = normalized[index];
        if (!book.bookId.empty() && !existing.bookId.empty() && book.bookId == existing.bookId) {
          return index;
        }
        if (!book.path.empty() && existing.path == book.path) {
          return index;
        }
      }
      return -1;
    }();

    if (existingIndex < 0) {
      normalized.push_back(book);
      continue;
    }

    auto& existing = normalized[existingIndex];
    if (existing.bookId.empty()) {
      existing.bookId = book.bookId;
    }
    if (existing.path.empty() || (!book.path.empty() && Storage.exists(book.path.c_str()))) {
      existing.path = book.path;
    }
    if (existing.title.empty() && !book.title.empty()) {
      existing.title = book.title;
    }
    if (existing.author.empty() && !book.author.empty()) {
      existing.author = book.author;
    }
    if (existing.coverBmpPath.empty() && !book.coverBmpPath.empty()) {
      existing.coverBmpPath = book.coverBmpPath;
    }
  }

  recentBooks = std::move(normalized);
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath, const std::string& bookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedPath.empty() ? BookIdentity::resolveStableBookId(normalizedPath) : "");

  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    recentBooks.erase(recentBooks.begin() + existingIndex);
  }

  recentBooks.insert(recentBooks.begin(), {resolvedBookId, normalizedPath, title, author, coverBmpPath});

  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath, const std::string& bookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedPath.empty() ? BookIdentity::resolveStableBookId(normalizedPath) : "");
  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    RecentBook& book = recentBooks[existingIndex];
    if (!resolvedBookId.empty()) {
      book.bookId = resolvedBookId;
    }
    if (!normalizedPath.empty()) {
      book.path = normalizedPath;
    }
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

bool RecentBooksStore::removeBook(const std::string& key) {
  const int existingIndex = findBookIndex(key, key);
  if (existingIndex < 0) {
    return false;
  }

  recentBooks.erase(recentBooks.begin() + existingIndex);
  saveToFile();
  return true;
}

bool RecentBooksStore::repairOrRemoveMissingBook(const std::string& key) {
  const int existingIndex = findBookIndex(key, key);
  if (existingIndex < 0) {
    return false;
  }

  RecentBook& book = recentBooks[existingIndex];
  if (!book.path.empty() && Storage.exists(book.path.c_str())) {
    return false;
  }

  const LibraryBookMetadata* metadata = nullptr;
  if (!book.bookId.empty()) {
    metadata = LIBRARY_METADATA.findBook(book.bookId);
  }
  if (metadata == nullptr && !book.path.empty()) {
    metadata = LIBRARY_METADATA.findBook(book.path);
  }

  if (metadata != nullptr) {
    auto applyCandidate = [this, &book, metadata](const std::string& candidatePath) {
      const std::string normalizedPath = BookIdentity::normalizePath(candidatePath);
      if (normalizedPath.empty() || !Storage.exists(normalizedPath.c_str())) {
        return false;
      }
      book.path = normalizedPath;
      if (!metadata->stableId.empty()) {
        book.bookId = metadata->stableId;
      } else if (!metadata->bookId.empty()) {
        book.bookId = metadata->bookId;
      }
      if (book.title.empty() && !metadata->title.empty()) {
        book.title = metadata->title;
      }
      if (book.author.empty() && !metadata->author.empty()) {
        book.author = metadata->author;
      }
      if (book.coverBmpPath.empty() && !metadata->coverPath.empty()) {
        book.coverBmpPath = metadata->coverPath;
      }
      normalizeBook(book);
      saveToFile();
      return true;
    };

    if (applyCandidate(metadata->path)) {
      return true;
    }
    for (const auto& knownPath : metadata->knownPaths) {
      if (applyCandidate(knownPath)) {
        return true;
      }
    }
  }

  recentBooks.erase(recentBooks.begin() + existingIndex);
  saveToFile();
  return true;
}

bool RecentBooksStore::repairOrRemoveMissingBooks() {
  bool changed = false;
  for (int index = static_cast<int>(recentBooks.size()) - 1; index >= 0; --index) {
    const RecentBook book = recentBooks[static_cast<size_t>(index)];
    if (!book.path.empty() && Storage.exists(book.path.c_str())) {
      continue;
    }
    const std::string key = !book.bookId.empty() ? book.bookId : book.path;
    if (key.empty()) {
      recentBooks.erase(recentBooks.begin() + index);
      changed = true;
      continue;
    }
    changed = repairOrRemoveMissingBook(key) || changed;
  }
  if (changed) {
    saveToFile();
  }
  return changed;
}

bool RecentBooksStore::repairRenamedBooks(const std::vector<std::string>& livePaths) {
  if (livePaths.empty() || recentBooks.empty()) {
    return false;
  }

  struct LiveBook {
    std::string path;
    std::string bookId;
    std::string fallbackKey;
  };
  auto normalizedFallbackKey = [](const std::string& path, const std::string& title, const std::string& author) {
    auto normalize = [](std::string value) {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      std::string token;
      std::vector<std::string> tokens;
      for (const char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
          token.push_back(ch);
        } else if (!token.empty()) {
          tokens.push_back(token);
          token.clear();
        }
      }
      if (!token.empty()) tokens.push_back(token);
      if (tokens.size() == 2) {
        std::sort(tokens.begin(), tokens.end());
      }
      value.clear();
      for (const auto& part : tokens) {
        value += part;
      }
      return value;
    };
    std::string resolvedTitle = title.empty() ? fallbackTitleFromPath(path) : title;
    std::string resolvedAuthor = author;
    if (resolvedAuthor.empty()) {
      const size_t slash = path.find_last_of('/');
      if (slash != std::string::npos && slash > 0) {
        const size_t parentEnd = slash - 1;
        const size_t parentStart = path.find_last_of('/', parentEnd);
        const size_t start = parentStart == std::string::npos ? 0 : parentStart + 1;
        resolvedAuthor = path.substr(start, parentEnd - start + 1);
      }
    }
    if (!resolvedAuthor.empty()) {
      const std::string suffix = " - " + resolvedAuthor;
      if (resolvedTitle.size() > suffix.size() &&
          resolvedTitle.compare(resolvedTitle.size() - suffix.size(), suffix.size(), suffix) == 0) {
        resolvedTitle.resize(resolvedTitle.size() - suffix.size());
      }
    }
    return normalize(resolvedAuthor) + "|" + normalize(resolvedTitle);
  };

  std::vector<LiveBook> liveBooks;
  liveBooks.reserve(livePaths.size());
  for (const auto& path : livePaths) {
    const std::string normalizedPath = BookIdentity::normalizePath(path);
    if (normalizedPath.empty() || !Storage.exists(normalizedPath.c_str())) {
      continue;
    }
    liveBooks.push_back(
        LiveBook{normalizedPath, BookIdentity::resolveStableBookId(normalizedPath),
                 normalizedFallbackKey(normalizedPath, "", "")});
  }

  bool changed = false;
  for (auto& book : recentBooks) {
    if (!book.path.empty() && Storage.exists(book.path.c_str())) {
      continue;
    }
    const std::string fallbackKey = normalizedFallbackKey(book.path, book.title, book.author);
    for (const auto& live : liveBooks) {
      const bool sameId = !book.bookId.empty() && !BookIdentity::isLegacyBookId(book.bookId) &&
                          !live.bookId.empty() && book.bookId == live.bookId;
      const bool sameFallback = !fallbackKey.empty() && fallbackKey == live.fallbackKey;
      if (!sameId && !sameFallback) {
        continue;
      }
      book.path = live.path;
      if (!live.bookId.empty()) book.bookId = live.bookId;
      changed = true;
      break;
    }
  }

  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), [](const RecentBook& book) {
                      return book.path.empty() || !Storage.exists(book.path.c_str());
                    }),
                    recentBooks.end());
  changed = changed || before != recentBooks.size();
  if (changed) {
    normalizeBooks();
    saveToFile();
  }
  return changed;
}

bool RecentBooksStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveRecentBooks(*this, RECENT_BOOKS_FILE_JSON);
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{BookIdentity::resolveStableBookId(path), path, epub.getTitle(), epub.getAuthor(),
                      epub.getThumbBmpPath()};
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{BookIdentity::resolveStableBookId(path), path, xtc.getTitle(), xtc.getAuthor(),
                        xtc.getThumbBmpPath()};
    }
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    return RecentBook{BookIdentity::resolveStableBookId(path), path, lastBookFileName, "", ""};
  }
  return RecentBook{BookIdentity::resolveStableBookId(path), path, "", "", ""};
}

bool RecentBooksStore::loadFromFile() {
  const std::string tempPath = std::string(RECENT_BOOKS_FILE_JSON) + ".tmp";
  if (!Storage.exists(RECENT_BOOKS_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    if (Storage.rename(tempPath.c_str(), RECENT_BOOKS_FILE_JSON)) {
      LOG_DBG("RBS", "Recovered recent.json from interrupted temp file");
    }
  }

  // Try JSON first
  if (Storage.exists(RECENT_BOOKS_FILE_JSON)) {
    String json = Storage.readFile(RECENT_BOOKS_FILE_JSON);
    if (!json.isEmpty()) {
      if (JsonSettingsIO::loadRecentBooks(*this, json.c_str())) {
        return true;
      }
      LOG_ERR("RBS", "recent.json could not be loaded; trying fallback sources");
    }
  }

  // Fall back to binary migration
  if (Storage.exists(RECENT_BOOKS_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      saveToFile();
      Storage.rename(RECENT_BOOKS_FILE_BIN, RECENT_BOOKS_FILE_BAK);
      LOG_DBG("RBS", "Migrated recent.bin to recent.json");
      return true;
    }
  }

  std::vector<const ReadingBookStats*> candidates;
  candidates.reserve(READING_STATS.getBooks().size());
  for (const auto& book : READING_STATS.getBooks()) {
    if (!book.path.empty() && Storage.exists(book.path.c_str())) {
      candidates.push_back(&book);
    }
  }
  std::sort(candidates.begin(), candidates.end(), [](const ReadingBookStats* a, const ReadingBookStats* b) {
    if (a->lastReadAt != b->lastReadAt) return a->lastReadAt > b->lastReadAt;
    return fallbackTitleFromPath(a->path) < fallbackTitleFromPath(b->path);
  });

  recentBooks.clear();
  recentBooks.reserve(std::min<int>(MAX_RECENT_BOOKS, static_cast<int>(candidates.size())));
  for (const auto* book : candidates) {
    if (recentBooks.size() >= MAX_RECENT_BOOKS) break;
    const std::string title = book->title.empty() ? fallbackTitleFromPath(book->path) : book->title;
    recentBooks.push_back({book->bookId, book->path, title, book->author, book->coverBmpPath});
  }
  if (!recentBooks.empty()) {
    normalizeBooks();
    saveToFile();
    LOG_DBG("RBS", "Rebuilt recent books from reading stats (%d entries)", static_cast<int>(recentBooks.size()));
    return true;
  }

  return false;
}

bool RecentBooksStore::loadFromBinaryFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("RBS", RECENT_BOOKS_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version == 1 || version == 2) {
    // Old version: migrate lightly to avoid opening EPUB/XTC during boot.
    uint8_t count;
    serialization::readPod(inputFile, count);
    recentBooks.clear();
    recentBooks.reserve(count);
    for (uint8_t i = 0; i < count; i++) {
      std::string path;
      serialization::readString(inputFile, path);
      std::string title;
      std::string author;
      if (version == 2) {
        serialization::readString(inputFile, title);
        serialization::readString(inputFile, author);
      }

      const std::string normalizedPath = BookIdentity::normalizePath(path);
      if (normalizedPath.empty()) {
        continue;
      }

      if (title.empty()) {
        title = fallbackTitleFromPath(normalizedPath);
      }

      recentBooks.push_back({BookIdentity::resolveStableBookId(normalizedPath), normalizedPath, title, author, ""});
    }
  } else if (version == 3) {
    uint8_t count;
    serialization::readPod(inputFile, count);

    recentBooks.clear();
    recentBooks.reserve(count);
    uint8_t omitted = 0;

    for (uint8_t i = 0; i < count; i++) {
      std::string path, title, author, coverBmpPath;
      serialization::readString(inputFile, path);
      serialization::readString(inputFile, title);
      serialization::readString(inputFile, author);
      serialization::readString(inputFile, coverBmpPath);

      // Omit books with missing title (e.g. saved before metadata was available)
      if (title.empty()) {
        omitted++;
        continue;
      }

      recentBooks.push_back({BookIdentity::resolveStableBookId(path), path, title, author, coverBmpPath});
    }

    if (omitted > 0) {
      // Explicitly close() file before saveToFile() rewrites the same file
      inputFile.close();
      saveToFile();
      LOG_DBG("RBS", "Omitted %u recent book(s) with missing title", omitted);
      return true;
    }
  } else {
    LOG_ERR("RBS", "Deserialization failed: Unknown version %u", version);
    return false;
  }

  inputFile.close();
  normalizeBooks();
  LOG_DBG("RBS", "Recent books loaded from binary file (%d entries)", static_cast<int>(recentBooks.size()));
  return true;
}
