#include "LibraryMetadataStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <ctime>

#include "util/BookIdentity.h"

namespace {
constexpr char LIBRARY_METADATA_FILE_JSON[] = "/.crosspoint/library_metadata.json";
constexpr uint8_t LIBRARY_METADATA_SCHEMA_VERSION = 1;

uint32_t nowOrZero() {
  const time_t now = time(nullptr);
  return now > 0 ? static_cast<uint32_t>(now) : 0;
}

bool sameBook(const LibraryBookMetadata& book, const std::string& normalizedPath, const std::string& bookId) {
  if (!bookId.empty() && !book.bookId.empty() && book.bookId == bookId) {
    return true;
  }
  return !normalizedPath.empty() && book.path == normalizedPath;
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
  if (book.bookId.empty() && !book.path.empty() && Storage.exists(book.path.c_str())) {
    book.bookId = BookIdentity::resolveStableBookId(book.path);
  }
}

void LibraryMetadataStore::normalizeBooks() {
  for (auto& book : books) {
    normalizeBook(book);
  }

  std::vector<LibraryBookMetadata> normalized;
  normalized.reserve(books.size());
  for (const auto& book : books) {
    if (!book.pinned && !book.toRead && !book.finished && !book.activeRemoved) {
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
    if (existing.bookId.empty()) existing.bookId = book.bookId;
    if (existing.path.empty()) existing.path = book.path;
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
    if (!resolvedBookId.empty()) existing.bookId = resolvedBookId;
    return existing;
  }

  books.push_back({resolvedBookId, normalizedPath});
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
  saveToFile();
}

bool LibraryMetadataStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["version"] = LIBRARY_METADATA_SCHEMA_VERSION;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    if (!book.pinned && !book.toRead && !book.finished && !book.activeRemoved) {
      continue;
    }
    JsonObject item = arr.add<JsonObject>();
    item["bookId"] = book.bookId;
    item["path"] = book.path;
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
    book.bookId = item["bookId"] | "";
    book.path = item["path"] | "";
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
