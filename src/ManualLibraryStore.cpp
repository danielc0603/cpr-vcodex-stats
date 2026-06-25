#include "ManualLibraryStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <ctime>

#include "util/BookIdentity.h"

namespace {
constexpr char MANUAL_LIBRARY_FILE_JSON[] = "/.crosspoint/manual_library.json";
constexpr uint8_t MANUAL_LIBRARY_SCHEMA_VERSION = 1;

uint32_t nowOrZero() {
  const time_t now = time(nullptr);
  return now > 0 ? static_cast<uint32_t>(now) : 0;
}

bool hasManualData(const ManualLibraryBook& book) {
  return !book.manualTitle.empty() || !book.manualAuthor.empty() || !book.manualSeries.empty() ||
         !book.manualSeriesNumber.empty() || !book.collections.empty() || !book.personalTags.empty() ||
         !book.customCover.empty() || !book.customThumbnail.empty() || !book.notes.empty() || book.rating > 0 ||
         book.favorite || book.pinned || book.hidden || book.archived || book.ignoreMetadataUpdates;
}
}  // namespace

ManualLibraryStore ManualLibraryStore::instance;

int ManualLibraryStore::findBookIndex(const std::string& path, const std::string& stableId) const {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  for (int index = 0; index < static_cast<int>(books.size()); ++index) {
    const auto& book = books[index];
    if (!stableId.empty() && !book.stableId.empty() && book.stableId == stableId) return index;
    if (!normalizedPath.empty() && book.path == normalizedPath) return index;
  }
  return -1;
}

void ManualLibraryStore::normalizeBook(ManualLibraryBook& book) {
  book.path = BookIdentity::normalizePath(book.path);
  if (book.stableId.empty() && !book.path.empty()) {
    book.stableId = BookIdentity::resolveStableBookId(book.path);
  }
}

void ManualLibraryStore::normalizeBooks() {
  for (auto& book : books) normalizeBook(book);

  std::vector<ManualLibraryBook> normalized;
  normalized.reserve(books.size());
  for (const auto& book : books) {
    if (!hasManualData(book)) continue;
    const int existingIndex = [&normalized, &book]() {
      for (int index = 0; index < static_cast<int>(normalized.size()); ++index) {
        const auto& existing = normalized[index];
        if (!book.stableId.empty() && !existing.stableId.empty() && book.stableId == existing.stableId) return index;
        if (!book.path.empty() && book.path == existing.path) return index;
      }
      return -1;
    }();
    if (existingIndex < 0) {
      normalized.push_back(book);
      continue;
    }
    auto& existing = normalized[existingIndex];
    if (existing.path.empty()) existing.path = book.path;
    if (existing.stableId.empty()) existing.stableId = book.stableId;
    if (existing.manualTitle.empty()) existing.manualTitle = book.manualTitle;
    if (existing.manualAuthor.empty()) existing.manualAuthor = book.manualAuthor;
    if (existing.manualSeries.empty()) existing.manualSeries = book.manualSeries;
    if (existing.manualSeriesNumber.empty()) existing.manualSeriesNumber = book.manualSeriesNumber;
    if (existing.customCover.empty()) existing.customCover = book.customCover;
    if (existing.customThumbnail.empty()) existing.customThumbnail = book.customThumbnail;
    if (existing.notes.empty()) existing.notes = book.notes;
    existing.rating = std::max(existing.rating, book.rating);
    existing.favorite = existing.favorite || book.favorite;
    existing.pinned = existing.pinned || book.pinned;
    existing.hidden = existing.hidden || book.hidden;
    existing.archived = existing.archived || book.archived;
    existing.ignoreMetadataUpdates = existing.ignoreMetadataUpdates || book.ignoreMetadataUpdates;
    existing.updatedAt = std::max(existing.updatedAt, book.updatedAt);
    for (const auto& collection : book.collections) {
      if (std::find(existing.collections.begin(), existing.collections.end(), collection) == existing.collections.end()) {
        existing.collections.push_back(collection);
      }
    }
    for (const auto& tag : book.personalTags) {
      if (std::find(existing.personalTags.begin(), existing.personalTags.end(), tag) == existing.personalTags.end()) {
        existing.personalTags.push_back(tag);
      }
    }
  }
  books = std::move(normalized);
}

ManualLibraryBook& ManualLibraryStore::getOrCreateBook(const std::string& path, const std::string& stableId) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedStableId =
      !stableId.empty() ? stableId : (!normalizedPath.empty() ? BookIdentity::resolveStableBookId(normalizedPath) : "");
  const int existingIndex = findBookIndex(normalizedPath, resolvedStableId);
  if (existingIndex >= 0) {
    auto& existing = books[existingIndex];
    if (!normalizedPath.empty()) existing.path = normalizedPath;
    if (!resolvedStableId.empty()) existing.stableId = resolvedStableId;
    return existing;
  }
  ManualLibraryBook book;
  book.path = normalizedPath;
  book.stableId = resolvedStableId;
  books.push_back(std::move(book));
  return books.back();
}

const ManualLibraryBook* ManualLibraryStore::findBook(const std::string& path, const std::string& stableId) const {
  const int index = findBookIndex(path, stableId.empty() ? BookIdentity::resolveStableBookId(path) : stableId);
  return index >= 0 ? &books[index] : nullptr;
}

bool ManualLibraryStore::setManualTitle(const std::string& path, const std::string& title,
                                        const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.manualTitle = title;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setManualAuthor(const std::string& path, const std::string& author,
                                         const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.manualAuthor = author;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setManualSeries(const std::string& path, const std::string& series, const std::string& number,
                                         const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.manualSeries = series;
  book.manualSeriesNumber = number;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setPersonalTags(const std::string& path, const std::string& tags,
                                         const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.personalTags.clear();
  size_t start = 0;
  while (start <= tags.size()) {
    const size_t comma = tags.find(',', start);
    const size_t end = comma == std::string::npos ? tags.size() : comma;
    std::string tag = tags.substr(start, end - start);
    tag.erase(tag.begin(), std::find_if(tag.begin(), tag.end(), [](unsigned char c) { return !std::isspace(c); }));
    tag.erase(std::find_if(tag.rbegin(), tag.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
              tag.end());
    if (!tag.empty() && std::find(book.personalTags.begin(), book.personalTags.end(), tag) == book.personalTags.end()) {
      book.personalTags.push_back(tag);
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setNotes(const std::string& path, const std::string& notes, const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.notes = notes;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setRating(const std::string& path, const uint8_t rating, const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.rating = std::min<uint8_t>(rating, 5);
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setCustomCover(const std::string& path, const std::string& coverPath,
                                        const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.customCover = coverPath;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setFavorite(const std::string& path, const bool favorite, const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.favorite = favorite;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setHidden(const std::string& path, const bool hidden, const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.hidden = hidden;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setArchived(const std::string& path, const bool archived, const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.archived = archived;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::setIgnoreMetadataUpdates(const std::string& path, const bool ignore,
                                                  const std::string& stableId) {
  auto& book = getOrCreateBook(path, stableId);
  book.ignoreMetadataUpdates = ignore;
  book.updatedAt = nowOrZero();
  normalizeBooks();
  return saveToFile();
}

bool ManualLibraryStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["version"] = MANUAL_LIBRARY_SCHEMA_VERSION;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    if (!hasManualData(book)) continue;
    JsonObject item = arr.add<JsonObject>();
    item["stableId"] = book.stableId;
    item["path"] = book.path;
    item["manualTitle"] = book.manualTitle;
    item["manualAuthor"] = book.manualAuthor;
    item["manualSeries"] = book.manualSeries;
    item["manualSeriesNumber"] = book.manualSeriesNumber;
    JsonArray collections = item["collections"].to<JsonArray>();
    for (const auto& collection : book.collections) collections.add(collection);
    JsonArray tags = item["personalTags"].to<JsonArray>();
    for (const auto& tag : book.personalTags) tags.add(tag);
    item["customCover"] = book.customCover;
    item["customThumbnail"] = book.customThumbnail;
    item["notes"] = book.notes;
    item["rating"] = book.rating;
    item["favorite"] = book.favorite;
    item["pinned"] = book.pinned;
    item["hidden"] = book.hidden;
    item["archived"] = book.archived;
    item["ignoreMetadataUpdates"] = book.ignoreMetadataUpdates;
    item["updatedAt"] = book.updatedAt;
  }

  const std::string tempPath = std::string(MANUAL_LIBRARY_FILE_JSON) + ".tmp";
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("MLIB", tempPath.c_str(), file)) {
    LOG_ERR("MLIB", "Could not open manual library temp file");
    return false;
  }
  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  if (written == 0) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (Storage.exists(MANUAL_LIBRARY_FILE_JSON) && !Storage.remove(MANUAL_LIBRARY_FILE_JSON)) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return Storage.rename(tempPath.c_str(), MANUAL_LIBRARY_FILE_JSON);
}

bool ManualLibraryStore::loadFromFile() {
  const std::string tempPath = std::string(MANUAL_LIBRARY_FILE_JSON) + ".tmp";
  if (!Storage.exists(MANUAL_LIBRARY_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    Storage.rename(tempPath.c_str(), MANUAL_LIBRARY_FILE_JSON);
  }
  if (!Storage.exists(MANUAL_LIBRARY_FILE_JSON)) return false;

  const String json = Storage.readFile(MANUAL_LIBRARY_FILE_JSON);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("MLIB", "Failed to parse manual library: %s", error.c_str());
    return false;
  }

  books.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  books.reserve(arr.size());
  for (JsonObject item : arr) {
    ManualLibraryBook book;
    book.stableId = item["stableId"] | "";
    book.path = item["path"] | "";
    book.manualTitle = item["manualTitle"] | "";
    book.manualAuthor = item["manualAuthor"] | "";
    book.manualSeries = item["manualSeries"] | "";
    book.manualSeriesNumber = item["manualSeriesNumber"] | "";
    for (JsonVariant collection : item["collections"].as<JsonArray>()) {
      book.collections.push_back(collection.as<std::string>());
    }
    for (JsonVariant tag : item["personalTags"].as<JsonArray>()) {
      book.personalTags.push_back(tag.as<std::string>());
    }
    book.customCover = item["customCover"] | "";
    book.customThumbnail = item["customThumbnail"] | "";
    book.notes = item["notes"] | "";
    book.rating = item["rating"] | 0;
    book.favorite = item["favorite"] | false;
    book.pinned = item["pinned"] | false;
    book.hidden = item["hidden"] | false;
    book.archived = item["archived"] | false;
    book.ignoreMetadataUpdates = item["ignoreMetadataUpdates"] | false;
    book.updatedAt = item["updatedAt"] | 0;
    books.push_back(std::move(book));
  }
  normalizeBooks();
  return true;
}
