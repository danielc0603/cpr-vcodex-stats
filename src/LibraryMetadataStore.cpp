#include "LibraryMetadataStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <ctime>

#include "BookMetadataStore.h"
#include "util/BookIdentity.h"

namespace {
constexpr char LIBRARY_METADATA_FILE_JSON[] = "/.crosspoint/library_metadata.json";
constexpr uint8_t LIBRARY_METADATA_SCHEMA_VERSION = 2;

uint32_t nowOrZero() {
  const time_t now = time(nullptr);
  return now > 0 ? static_cast<uint32_t>(now) : 0;
}

bool sameBook(const LibraryBookMetadata& book, const std::string& normalizedPath, const std::string& bookId) {
  if (!bookId.empty() && !book.stableId.empty() && book.stableId == bookId) {
    return true;
  }
  if (!bookId.empty() && !book.bookId.empty() && book.bookId == bookId) {
    return true;
  }
  if (!bookId.empty() && !book.epubIdentifier.empty() && book.epubIdentifier == bookId) {
    return true;
  }
  return !normalizedPath.empty() && book.path == normalizedPath;
}

std::string extensionFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  return path.substr(dot + 1);
}

std::string filenameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

void setIfPresent(std::string& target, const std::string& value) {
  if (!value.empty()) target = value;
}
}  // namespace

LibraryMetadataStore LibraryMetadataStore::instance;

int LibraryMetadataStore::findBookIndex(const std::string& path, const std::string& bookId) const {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  for (int index = 0; index < static_cast<int>(books.size()); ++index) {
    if (sameBook(books[index], normalizedPath, bookId)) {
      return index;
    }
  }
  return -1;
}

void LibraryMetadataStore::normalizeBook(LibraryBookMetadata& book) {
  book.path = BookIdentity::normalizePath(book.path);
  if (book.stableId.empty() && !book.path.empty()) {
    book.stableId = BookIdentity::resolveStableBookId(book.path);
  }
  if (book.bookId.empty() && !book.path.empty() && Storage.exists(book.path.c_str())) {
    book.bookId = BookIdentity::resolveStableBookId(book.path);
  }
  if (book.epubIdentifier.empty()) book.epubIdentifier = book.bookId;
  if (book.filename.empty() && !book.path.empty()) book.filename = filenameFromPath(book.path);
  if (book.extension.empty() && !book.path.empty()) book.extension = extensionFromPath(book.path);
  if (book.format.empty() && !book.extension.empty()) book.format = book.extension;
  if (book.fileSize == 0 && !book.path.empty() && Storage.exists(book.path.c_str())) {
    FsFile file = Storage.open(book.path.c_str());
    if (file && !file.isDirectory()) book.fileSize = file.size();
    if (file) file.close();
  }
  rememberKnownPath(book, book.path);
  if (book.lastMetadataScan == 0) book.lastMetadataScan = book.updatedAt;
  if (book.lastMetadataUpdate == 0) book.lastMetadataUpdate = book.updatedAt;
}

void LibraryMetadataStore::rememberKnownPath(LibraryBookMetadata& book, const std::string& path) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  if (normalizedPath.empty()) return;
  if (std::find(book.knownPaths.begin(), book.knownPaths.end(), normalizedPath) == book.knownPaths.end()) {
    book.knownPaths.push_back(normalizedPath);
  }
}

void LibraryMetadataStore::normalizeBooks() {
  for (auto& book : books) {
    normalizeBook(book);
  }

  std::vector<LibraryBookMetadata> normalized;
  normalized.reserve(books.size());
  for (const auto& book : books) {
    const bool hasMetadata = !book.title.empty() || !book.author.empty() || !book.seriesName.empty() ||
                             !book.publisher.empty() || !book.tags.empty() || !book.description.empty() ||
                             !book.coverPath.empty() || !book.metadataSource.empty();
    if (!hasMetadata && !book.pinned && !book.toRead && !book.finished && !book.activeRemoved) {
      continue;
    }

    const int existingIndex = [&normalized, &book]() {
      for (int index = 0; index < static_cast<int>(normalized.size()); ++index) {
        if (sameBook(normalized[index], book.path, book.bookId)) {
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
    existing.pinned = existing.pinned || book.pinned;
    existing.toRead = existing.toRead || book.toRead;
    existing.finished = existing.finished || book.finished;
    existing.activeRemoved = existing.activeRemoved || book.activeRemoved;
    existing.updatedAt = std::max(existing.updatedAt, book.updatedAt);
    existing.lastMetadataScan = std::max(existing.lastMetadataScan, book.lastMetadataScan);
    existing.lastMetadataUpdate = std::max(existing.lastMetadataUpdate, book.lastMetadataUpdate);
    if (existing.bookId.empty()) existing.bookId = book.bookId;
    if (existing.stableId.empty()) existing.stableId = book.stableId;
    if (existing.epubIdentifier.empty()) existing.epubIdentifier = book.epubIdentifier;
    if (existing.path.empty()) existing.path = book.path;
    for (const auto& path : book.knownPaths) rememberKnownPath(existing, path);
  }
  books = std::move(normalized);
}

LibraryBookMetadata& LibraryMetadataStore::getOrCreateBook(const std::string& path, const std::string& bookId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedPath.empty() && Storage.exists(normalizedPath.c_str())
                                      ? BookIdentity::resolveStableBookId(normalizedPath)
                                      : "");
  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    auto& existing = books[existingIndex];
    if (!normalizedPath.empty()) existing.path = normalizedPath;
    if (!resolvedBookId.empty()) {
      existing.bookId = resolvedBookId;
      if (existing.stableId.empty()) existing.stableId = resolvedBookId;
      if (existing.epubIdentifier.empty()) existing.epubIdentifier = resolvedBookId;
    }
    rememberKnownPath(existing, normalizedPath);
    return existing;
  }

  LibraryBookMetadata book;
  book.stableId = resolvedBookId;
  book.bookId = resolvedBookId;
  book.epubIdentifier = resolvedBookId;
  book.path = normalizedPath;
  book.filename = filenameFromPath(normalizedPath);
  book.extension = extensionFromPath(normalizedPath);
  book.format = book.extension;
  rememberKnownPath(book, normalizedPath);
  books.push_back(std::move(book));
  return books.back();
}

const LibraryBookMetadata* LibraryMetadataStore::findBook(const std::string& key) const {
  const int index = findBookIndex(key, key);
  return index >= 0 ? &books[index] : nullptr;
}

bool LibraryMetadataStore::isPinned(const std::string& key) const {
  const auto* book = findBook(key);
  return book != nullptr && book->pinned;
}

bool LibraryMetadataStore::isToRead(const std::string& key) const {
  const auto* book = findBook(key);
  return book != nullptr && book->toRead;
}

bool LibraryMetadataStore::isFinished(const std::string& key) const {
  const auto* book = findBook(key);
  return book != nullptr && book->finished;
}

bool LibraryMetadataStore::isActiveRemoved(const std::string& key) const {
  const auto* book = findBook(key);
  return book != nullptr && book->activeRemoved;
}

uint8_t LibraryMetadataStore::cycleShelfState(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  uint8_t result = 0;
  if (!book.toRead && !book.pinned) {
    book.toRead = true;
    result = 1;
  } else if (book.toRead && !book.pinned) {
    book.toRead = false;
    book.pinned = true;
    result = 2;
  } else {
    book.toRead = false;
    book.pinned = false;
  }
  book.updatedAt = nowOrZero();
  normalizeBooks();
  saveToFile();
  return result;
}

void LibraryMetadataStore::setToRead(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  book.toRead = true;
  book.finished = false;
  book.pinned = false;
  book.activeRemoved = false;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  saveToFile();
}

void LibraryMetadataStore::setFinished(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  book.finished = true;
  book.toRead = false;
  book.pinned = false;
  book.activeRemoved = false;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  saveToFile();
}

void LibraryMetadataStore::removeFromToRead(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  book.toRead = false;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  saveToFile();
}

void LibraryMetadataStore::removeFinishedState(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  book.finished = false;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  saveToFile();
}

void LibraryMetadataStore::removeActiveReadingState(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  book.toRead = false;
  book.finished = false;
  book.pinned = false;
  book.activeRemoved = true;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  saveToFile();
}

void LibraryMetadataStore::clearReadingState(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  book.pinned = false;
  book.toRead = false;
  book.finished = false;
  book.activeRemoved = false;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  saveToFile();
}

void LibraryMetadataStore::rememberBook(const std::string& path, const std::string& bookId) {
  auto& book = getOrCreateBook(path, bookId);
  book.updatedAt = nowOrZero();
  book.lastMetadataScan = book.updatedAt;
  saveToFile();
}

bool LibraryMetadataStore::upsertBookMetadata(const std::string& path, const std::string& bookId,
                                              const CachedBookMetadata& metadata, const std::string& source,
                                              const bool persist) {
  if (path.empty() && bookId.empty()) return false;
  auto& book = getOrCreateBook(path, bookId.empty() ? metadata.bookId : bookId);
  const uint32_t now = nowOrZero();
  const std::string normalizedPath = BookIdentity::normalizePath(path.empty() ? metadata.path : path);
  if (!normalizedPath.empty()) {
    book.path = normalizedPath;
    rememberKnownPath(book, normalizedPath);
    book.filename = filenameFromPath(normalizedPath);
    book.extension = extensionFromPath(normalizedPath);
    if (book.format.empty()) book.format = book.extension;
    if (Storage.exists(normalizedPath.c_str())) {
      FsFile file = Storage.open(normalizedPath.c_str());
      if (file && !file.isDirectory()) book.fileSize = file.size();
      if (file) file.close();
    }
  }
  setIfPresent(book.stableId, BookIdentity::resolveStableBookId(book.path));
  setIfPresent(book.bookId, metadata.bookId.empty() ? bookId : metadata.bookId);
  setIfPresent(book.epubIdentifier, metadata.identifier.empty() ? metadata.bookId : metadata.identifier);
  setIfPresent(book.title, metadata.title);
  setIfPresent(book.author, metadata.author);
  setIfPresent(book.sortTitle, metadata.title);
  setIfPresent(book.sortAuthor, metadata.author);
  setIfPresent(book.language, metadata.language);
  setIfPresent(book.publisher, metadata.publisher);
  setIfPresent(book.seriesName, metadata.series);
  setIfPresent(book.seriesIndex, metadata.seriesIndex);
  setIfPresent(book.seriesNumber, metadata.seriesIndex);
  setIfPresent(book.tags, metadata.tags);
  setIfPresent(book.subjects, metadata.tags);
  setIfPresent(book.keywords, metadata.tags);
  setIfPresent(book.description, metadata.description);
  setIfPresent(book.blurb, metadata.description);
  setIfPresent(book.coverPath, metadata.coverPath);
  setIfPresent(book.metadataSource, source.empty() ? metadata.source : source);
  book.metadataVersion = LIBRARY_METADATA_SCHEMA_VERSION;
  book.lastMetadataScan = now;
  book.lastMetadataUpdate = now;
  book.updatedAt = now;
  normalizeBooks();
  return !persist || saveToFile();
}

bool LibraryMetadataStore::repairRenamedBooks(const std::vector<std::string>& livePaths) {
  if (livePaths.empty() || books.empty()) {
    return false;
  }

  struct LiveBook {
    std::string path;
    std::string bookId;
    std::string fallbackKey;
  };
  auto normalizedFallbackKey = [](const std::string& path) {
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
    std::string title = path;
    const size_t slash = title.find_last_of('/');
    if (slash != std::string::npos) title = title.substr(slash + 1);
    const size_t dot = title.find_last_of('.');
    if (dot != std::string::npos) title = title.substr(0, dot);
    std::string author;
    if (slash != std::string::npos && slash > 0) {
      const size_t parentEnd = slash - 1;
      const size_t parentStart = path.find_last_of('/', parentEnd);
      const size_t start = parentStart == std::string::npos ? 0 : parentStart + 1;
      author = path.substr(start, parentEnd - start + 1);
    }
    if (!author.empty()) {
      const std::string suffix = " - " + author;
      if (title.size() > suffix.size() && title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0) {
        title.resize(title.size() - suffix.size());
      }
    }
    return normalize(author) + "|" + normalize(title);
  };

  std::vector<LiveBook> liveBooks;
  liveBooks.reserve(livePaths.size());
  for (const auto& path : livePaths) {
    const std::string normalizedPath = BookIdentity::normalizePath(path);
    if (normalizedPath.empty() || !Storage.exists(normalizedPath.c_str())) {
      continue;
    }
    liveBooks.push_back(
        LiveBook{normalizedPath, BookIdentity::resolveStableBookId(normalizedPath), normalizedFallbackKey(normalizedPath)});
  }

  bool changed = false;
  for (auto& book : books) {
    if (!book.path.empty() && Storage.exists(book.path.c_str())) {
      continue;
    }
    const std::string fallbackKey = normalizedFallbackKey(book.path);
    for (const auto& live : liveBooks) {
      const bool sameId = !book.bookId.empty() && !BookIdentity::isLegacyBookId(book.bookId) &&
                          !live.bookId.empty() && book.bookId == live.bookId;
      const bool sameFallback = !fallbackKey.empty() && fallbackKey == live.fallbackKey;
      if (!sameId && !sameFallback) {
        continue;
      }
      book.path = live.path;
      if (!live.bookId.empty()) book.bookId = live.bookId;
      book.updatedAt = nowOrZero();
      changed = true;
      break;
    }
  }
  if (changed) {
    normalizeBooks();
    saveToFile();
  }
  return changed;
}

bool LibraryMetadataStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["version"] = LIBRARY_METADATA_SCHEMA_VERSION;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    const bool hasMetadata = !book.title.empty() || !book.author.empty() || !book.seriesName.empty() ||
                             !book.publisher.empty() || !book.tags.empty() || !book.description.empty() ||
                             !book.coverPath.empty() || !book.metadataSource.empty();
    if (!hasMetadata && !book.pinned && !book.toRead && !book.finished && !book.activeRemoved) {
      continue;
    }
    JsonObject item = arr.add<JsonObject>();
    auto addString = [&item](const char* key, const std::string& value) {
      if (!value.empty()) item[key] = value;
    };
    auto addU32 = [&item](const char* key, const uint32_t value) {
      if (value > 0) item[key] = value;
    };
    auto addU16 = [&item](const char* key, const uint16_t value) {
      if (value > 0) item[key] = value;
    };
    addString("stableId", book.stableId);
    addString("bookId", book.bookId);
    addString("epubIdentifier", book.epubIdentifier);
    addString("path", book.path);
    if (!book.knownPaths.empty()) {
      JsonArray knownPaths = item["knownPaths"].to<JsonArray>();
      for (const auto& knownPath : book.knownPaths) knownPaths.add(knownPath);
    }
    addString("filename", book.filename);
    addString("extension", book.extension);
    addString("title", book.title);
    addString("subtitle", book.subtitle);
    addString("author", book.author);
    addString("sortTitle", book.sortTitle);
    addString("sortAuthor", book.sortAuthor);
    addString("language", book.language);
    addString("publisher", book.publisher);
    addString("publishedDate", book.publishedDate);
    addString("edition", book.edition);
    addString("copyright", book.copyright);
    addString("seriesName", book.seriesName);
    addString("seriesNumber", book.seriesNumber);
    addString("seriesIndex", book.seriesIndex);
    addString("seriesTotal", book.seriesTotal);
    addString("isbn", book.isbn);
    addString("asin", book.asin);
    addString("doi", book.doi);
    addString("calibreId", book.calibreId);
    addString("uuid", book.uuid);
    addString("genre", book.genre);
    addString("subjects", book.subjects);
    addString("tags", book.tags);
    addString("keywords", book.keywords);
    addString("description", book.description);
    addString("shortDescription", book.shortDescription);
    addString("blurb", book.blurb);
    addString("coverPath", book.coverPath);
    addString("coverHash", book.coverHash);
    addU16("coverWidth", book.coverWidth);
    addU16("coverHeight", book.coverHeight);
    addU32("coverLastGenerated", book.coverLastGenerated);
    addString("format", book.format);
    addU32("fileSize", book.fileSize);
    addU32("pageCount", book.pageCount);
    addU32("chapterCount", book.chapterCount);
    addU32("wordCount", book.wordCount);
    addU32("estimatedReadingTime", book.estimatedReadingTime);
    addString("metadataSource", book.metadataSource);
    addU16("metadataVersion", book.metadataVersion);
    addU32("lastMetadataScan", book.lastMetadataScan);
    addU32("lastMetadataUpdate", book.lastMetadataUpdate);
    item["pinned"] = book.pinned;
    item["toRead"] = book.toRead;
    item["finished"] = book.finished;
    item["activeRemoved"] = book.activeRemoved;
    item["updatedAt"] = book.updatedAt;
  }

  const std::string tempPath = std::string(LIBRARY_METADATA_FILE_JSON) + ".tmp";
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile file;
  if (!Storage.openFileForWrite("LIB", tempPath.c_str(), file)) {
    LOG_ERR("LIB", "Could not open library metadata temp file");
    return false;
  }
  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  if (written == 0) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (Storage.exists(LIBRARY_METADATA_FILE_JSON) && !Storage.remove(LIBRARY_METADATA_FILE_JSON)) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return Storage.rename(tempPath.c_str(), LIBRARY_METADATA_FILE_JSON);
}

bool LibraryMetadataStore::loadFromFile() {
  const std::string tempPath = std::string(LIBRARY_METADATA_FILE_JSON) + ".tmp";
  if (!Storage.exists(LIBRARY_METADATA_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    Storage.rename(tempPath.c_str(), LIBRARY_METADATA_FILE_JSON);
  }
  if (!Storage.exists(LIBRARY_METADATA_FILE_JSON)) {
    return false;
  }

  const String json = Storage.readFile(LIBRARY_METADATA_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("LIB", "Failed to parse library metadata: %s", error.c_str());
    return false;
  }

  books.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  books.reserve(arr.size());
  for (JsonObject item : arr) {
    LibraryBookMetadata book;
    book.stableId = item["stableId"] | "";
    book.bookId = item["bookId"] | "";
    book.epubIdentifier = item["epubIdentifier"] | "";
    book.path = item["path"] | "";
    for (JsonVariant path : item["knownPaths"].as<JsonArray>()) {
      book.knownPaths.push_back(path.as<std::string>());
    }
    book.filename = item["filename"] | "";
    book.extension = item["extension"] | "";
    book.title = item["title"] | "";
    book.subtitle = item["subtitle"] | "";
    book.author = item["author"] | "";
    book.sortTitle = item["sortTitle"] | "";
    book.sortAuthor = item["sortAuthor"] | "";
    book.language = item["language"] | "";
    book.publisher = item["publisher"] | "";
    book.publishedDate = item["publishedDate"] | "";
    book.edition = item["edition"] | "";
    book.copyright = item["copyright"] | "";
    book.seriesName = item["seriesName"] | "";
    book.seriesNumber = item["seriesNumber"] | "";
    book.seriesIndex = item["seriesIndex"] | "";
    book.seriesTotal = item["seriesTotal"] | "";
    book.isbn = item["isbn"] | "";
    book.asin = item["asin"] | "";
    book.doi = item["doi"] | "";
    book.calibreId = item["calibreId"] | "";
    book.uuid = item["uuid"] | "";
    book.genre = item["genre"] | "";
    book.subjects = item["subjects"] | "";
    book.tags = item["tags"] | "";
    book.keywords = item["keywords"] | "";
    book.description = item["description"] | "";
    book.shortDescription = item["shortDescription"] | "";
    book.blurb = item["blurb"] | "";
    book.coverPath = item["coverPath"] | "";
    book.coverHash = item["coverHash"] | "";
    book.coverWidth = item["coverWidth"] | 0;
    book.coverHeight = item["coverHeight"] | 0;
    book.coverLastGenerated = item["coverLastGenerated"] | 0;
    book.format = item["format"] | "";
    book.fileSize = item["fileSize"] | 0;
    book.pageCount = item["pageCount"] | 0;
    book.chapterCount = item["chapterCount"] | 0;
    book.wordCount = item["wordCount"] | 0;
    book.estimatedReadingTime = item["estimatedReadingTime"] | 0;
    book.metadataSource = item["metadataSource"] | "";
    book.metadataVersion = item["metadataVersion"] | 0;
    book.lastMetadataScan = item["lastMetadataScan"] | 0;
    book.lastMetadataUpdate = item["lastMetadataUpdate"] | 0;
    book.pinned = item["pinned"] | false;
    book.toRead = item["toRead"] | false;
    book.finished = item["finished"] | false;
    book.activeRemoved = item["activeRemoved"] | false;
    book.updatedAt = item["updatedAt"] | 0;
    books.push_back(std::move(book));
  }
  normalizeBooks();
  return true;
}
