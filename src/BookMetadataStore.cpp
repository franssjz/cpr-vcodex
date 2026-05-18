#include "BookMetadataStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <memory>

#include "FsHelpers.h"
#include "util/BookIdentity.h"

namespace {
constexpr char BOOK_METADATA_FILE_JSON[] = "/.crosspoint/book_metadata.json";
constexpr uint8_t BOOK_METADATA_SCHEMA_VERSION = 1;
constexpr size_t MAX_SHORT_FIELD_LENGTH = 160;
constexpr size_t MAX_DESCRIPTION_LENGTH = 700;
constexpr size_t MAX_OPF_READ_BYTES = 32768;

uint32_t nowOrZero() {
  const time_t now = time(nullptr);
  return now > 0 ? static_cast<uint32_t>(now) : 0;
}

std::string clampField(std::string value, const size_t maxLength) {
  if (value.size() > maxLength) {
    value.resize(maxLength);
  }
  return value;
}

bool sameBook(const CachedBookMetadata& book, const std::string& normalizedPath, const std::string& bookId) {
  if (!bookId.empty() && !book.bookId.empty() && book.bookId == bookId) {
    return true;
  }
  return !normalizedPath.empty() && book.path == normalizedPath;
}

bool assignIfPresent(std::string& target, const std::string& source, const size_t maxLength) {
  if (source.empty()) {
    return false;
  }
  const std::string clamped = clampField(source, maxLength);
  if (target == clamped) {
    return false;
  }
  target = clamped;
  return true;
}

void replaceAll(std::string& value, const std::string& from, const std::string& to) {
  size_t pos = 0;
  while ((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.length(), to);
    pos += to.length();
  }
}

std::string trim(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    start++;
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    end--;
  }
  return value.substr(start, end - start);
}

std::string decodeXmlEntities(std::string value) {
  replaceAll(value, "&amp;", "&");
  replaceAll(value, "&quot;", "\"");
  replaceAll(value, "&apos;", "'");
  replaceAll(value, "&lt;", "<");
  replaceAll(value, "&gt;", ">");
  return trim(value);
}

std::string folderForPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? "/" : path.substr(0, slash + 1);
}

std::string basenameForPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string withoutExtension(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  const size_t slash = path.find_last_of('/');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    return path;
  }
  return path.substr(0, dot);
}

bool isSupportedBookPath(const std::string& path) {
  return FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) || FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);
}

bool hasUsefulMetadata(const CachedBookMetadata& metadata) {
  return !metadata.title.empty() || !metadata.author.empty() || !metadata.series.empty() ||
         !metadata.seriesIndex.empty() || !metadata.tags.empty() || !metadata.publisher.empty() ||
         !metadata.language.empty() || !metadata.description.empty() || !metadata.identifier.empty() ||
         !metadata.coverPath.empty();
}

std::string extractTag(const std::string& xml, const char* tag) {
  const std::string openNeedle = std::string("<") + tag;
  const std::string closeNeedle = std::string("</") + tag + ">";
  const size_t open = xml.find(openNeedle);
  if (open == std::string::npos) return "";
  const size_t contentStart = xml.find('>', open);
  if (contentStart == std::string::npos) return "";
  const size_t close = xml.find(closeNeedle, contentStart + 1);
  if (close == std::string::npos) return "";
  return decodeXmlEntities(xml.substr(contentStart + 1, close - contentStart - 1));
}

std::string extractMetaContent(const std::string& xml, const char* name) {
  const std::string nameNeedle = std::string("name=\"") + name + "\"";
  const size_t namePos = xml.find(nameNeedle);
  if (namePos == std::string::npos) return "";
  const size_t tagStart = xml.rfind("<meta", namePos);
  const size_t tagEnd = xml.find('>', namePos);
  if (tagStart == std::string::npos || tagEnd == std::string::npos) return "";
  const size_t contentPos = xml.find("content=\"", tagStart);
  if (contentPos == std::string::npos || contentPos > tagEnd) return "";
  const size_t valueStart = contentPos + 9;
  const size_t valueEnd = xml.find('"', valueStart);
  if (valueEnd == std::string::npos || valueEnd > tagEnd) return "";
  return decodeXmlEntities(xml.substr(valueStart, valueEnd - valueStart));
}

std::string extractSubjects(const std::string& xml) {
  std::string tags;
  size_t searchFrom = 0;
  for (int count = 0; count < 4; ++count) {
    const size_t tagPos = xml.find("<dc:subject", searchFrom);
    if (tagPos == std::string::npos) break;
    const size_t contentStart = xml.find('>', tagPos);
    const size_t close = xml.find("</dc:subject>", contentStart == std::string::npos ? tagPos : contentStart);
    if (contentStart == std::string::npos || close == std::string::npos) break;
    const std::string subject = decodeXmlEntities(xml.substr(contentStart + 1, close - contentStart - 1));
    if (!subject.empty()) {
      if (!tags.empty()) tags += ", ";
      tags += subject;
    }
    searchFrom = close + 13;
  }
  return tags;
}

CachedBookMetadata parseOpfFile(const std::string& opfPath) {
  CachedBookMetadata metadata;
  if (!Storage.exists(opfPath.c_str())) return metadata;

  auto buffer = std::make_unique<char[]>(MAX_OPF_READ_BYTES + 1);
  const size_t bytesRead = Storage.readFileToBuffer(opfPath.c_str(), buffer.get(), MAX_OPF_READ_BYTES + 1,
                                                    MAX_OPF_READ_BYTES);
  if (bytesRead == 0) return metadata;
  buffer[bytesRead] = '\0';
  const std::string xml(buffer.get(), bytesRead);

  metadata.title = extractTag(xml, "dc:title");
  metadata.author = extractTag(xml, "dc:creator");
  metadata.language = extractTag(xml, "dc:language");
  metadata.publisher = extractTag(xml, "dc:publisher");
  metadata.description = extractTag(xml, "dc:description");
  metadata.identifier = extractTag(xml, "dc:identifier");
  metadata.tags = extractSubjects(xml);
  metadata.series = extractMetaContent(xml, "calibre:series");
  metadata.seriesIndex = extractMetaContent(xml, "calibre:series_index");
  return metadata;
}

std::vector<std::string> sidecarCandidatesForBook(const std::string& bookPath) {
  const std::string folder = folderForPath(bookPath);
  return {withoutExtension(bookPath) + ".opf", folder + "metadata.opf"};
}

void mergeOpfOverFallback(CachedBookMetadata& target, const CachedBookMetadata& source) {
  assignIfPresent(target.title, source.title, MAX_SHORT_FIELD_LENGTH);
  assignIfPresent(target.author, source.author, MAX_SHORT_FIELD_LENGTH);
  assignIfPresent(target.series, source.series, MAX_SHORT_FIELD_LENGTH);
  assignIfPresent(target.seriesIndex, source.seriesIndex, 32);
  assignIfPresent(target.tags, source.tags, MAX_SHORT_FIELD_LENGTH);
  assignIfPresent(target.publisher, source.publisher, MAX_SHORT_FIELD_LENGTH);
  assignIfPresent(target.language, source.language, 48);
  assignIfPresent(target.description, source.description, MAX_DESCRIPTION_LENGTH);
  assignIfPresent(target.identifier, source.identifier, MAX_SHORT_FIELD_LENGTH);
  assignIfPresent(target.coverPath, source.coverPath, MAX_SHORT_FIELD_LENGTH);
}
}  // namespace

BookMetadataStore BookMetadataStore::instance;

void BookMetadataStore::ensureLoaded() {
  if (!loaded) {
    loadFromFile();
    loaded = true;
  }
}

int BookMetadataStore::findBookIndex(const std::string& path, const std::string& bookId) const {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  for (int index = 0; index < static_cast<int>(books.size()); ++index) {
    if (sameBook(books[index], normalizedPath, bookId)) {
      return index;
    }
  }
  return -1;
}

void BookMetadataStore::normalizeBook(CachedBookMetadata& book) const {
  book.path = BookIdentity::normalizePath(book.path);
  if (book.bookId.empty() && !book.path.empty() && Storage.exists(book.path.c_str())) {
    book.bookId = BookIdentity::resolveStableBookId(book.path);
  }
  book.title = clampField(book.title, MAX_SHORT_FIELD_LENGTH);
  book.author = clampField(book.author, MAX_SHORT_FIELD_LENGTH);
  book.series = clampField(book.series, MAX_SHORT_FIELD_LENGTH);
  book.seriesIndex = clampField(book.seriesIndex, 32);
  book.tags = clampField(book.tags, MAX_SHORT_FIELD_LENGTH);
  book.publisher = clampField(book.publisher, MAX_SHORT_FIELD_LENGTH);
  book.language = clampField(book.language, 48);
  book.description = clampField(book.description, MAX_DESCRIPTION_LENGTH);
  book.identifier = clampField(book.identifier, MAX_SHORT_FIELD_LENGTH);
  book.coverPath = clampField(book.coverPath, MAX_SHORT_FIELD_LENGTH);
  book.source = clampField(book.source, 48);
}

void BookMetadataStore::normalizeBooks() {
  for (auto& book : books) {
    normalizeBook(book);
  }

  std::vector<CachedBookMetadata> normalized;
  normalized.reserve(books.size());
  for (const auto& book : books) {
    if (book.path.empty() && book.bookId.empty()) {
      continue;
    }

    const auto existing = std::find_if(normalized.begin(), normalized.end(), [&book](const CachedBookMetadata& other) {
      return sameBook(other, book.path, book.bookId);
    });
    if (existing == normalized.end()) {
      normalized.push_back(book);
      continue;
    }

    CachedBookMetadata& target = *existing;
    assignIfPresent(target.title, book.title, MAX_SHORT_FIELD_LENGTH);
    assignIfPresent(target.author, book.author, MAX_SHORT_FIELD_LENGTH);
    assignIfPresent(target.series, book.series, MAX_SHORT_FIELD_LENGTH);
    assignIfPresent(target.seriesIndex, book.seriesIndex, 32);
    assignIfPresent(target.tags, book.tags, MAX_SHORT_FIELD_LENGTH);
    assignIfPresent(target.publisher, book.publisher, MAX_SHORT_FIELD_LENGTH);
    assignIfPresent(target.language, book.language, 48);
    assignIfPresent(target.description, book.description, MAX_DESCRIPTION_LENGTH);
    assignIfPresent(target.identifier, book.identifier, MAX_SHORT_FIELD_LENGTH);
    assignIfPresent(target.coverPath, book.coverPath, MAX_SHORT_FIELD_LENGTH);
    assignIfPresent(target.source, book.source, 48);
    target.updatedAt = std::max(target.updatedAt, book.updatedAt);
    if (target.bookId.empty()) target.bookId = book.bookId;
    if (target.path.empty()) target.path = book.path;
  }
  books = std::move(normalized);
}

CachedBookMetadata& BookMetadataStore::getOrCreateBook(const std::string& path, const std::string& bookId) {
  ensureLoaded();
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  const std::string resolvedBookId =
      !bookId.empty() ? bookId : (!normalizedPath.empty() && Storage.exists(normalizedPath.c_str())
                                      ? BookIdentity::resolveStableBookId(normalizedPath)
                                      : "");
  const int existingIndex = findBookIndex(normalizedPath, resolvedBookId);
  if (existingIndex >= 0) {
    CachedBookMetadata& existing = books[existingIndex];
    if (!normalizedPath.empty()) existing.path = normalizedPath;
    if (!resolvedBookId.empty()) existing.bookId = resolvedBookId;
    return existing;
  }

  books.push_back({resolvedBookId, normalizedPath});
  return books.back();
}

const CachedBookMetadata* BookMetadataStore::findBook(const std::string& path, const std::string& bookId) {
  ensureLoaded();
  const int index = findBookIndex(path, bookId);
  return index >= 0 ? &books[index] : nullptr;
}

bool BookMetadataStore::mergeMetadata(const std::string& path, const std::string& bookId,
                                      const CachedBookMetadata& metadata, const std::string& source) {
  CachedBookMetadata& book = getOrCreateBook(path, bookId);
  bool changed = false;
  changed |= assignIfPresent(book.title, metadata.title, MAX_SHORT_FIELD_LENGTH);
  changed |= assignIfPresent(book.author, metadata.author, MAX_SHORT_FIELD_LENGTH);
  changed |= assignIfPresent(book.series, metadata.series, MAX_SHORT_FIELD_LENGTH);
  changed |= assignIfPresent(book.seriesIndex, metadata.seriesIndex, 32);
  changed |= assignIfPresent(book.tags, metadata.tags, MAX_SHORT_FIELD_LENGTH);
  changed |= assignIfPresent(book.publisher, metadata.publisher, MAX_SHORT_FIELD_LENGTH);
  changed |= assignIfPresent(book.language, metadata.language, 48);
  changed |= assignIfPresent(book.description, metadata.description, MAX_DESCRIPTION_LENGTH);
  changed |= assignIfPresent(book.identifier, metadata.identifier, MAX_SHORT_FIELD_LENGTH);
  changed |= assignIfPresent(book.coverPath, metadata.coverPath, MAX_SHORT_FIELD_LENGTH);
  changed |= assignIfPresent(book.source, source, 48);
  if (changed) {
    book.updatedAt = nowOrZero();
    normalizeBooks();
    return saveToFile();
  }
  return true;
}

bool BookMetadataStore::importSidecarForBook(const std::string& path, const CachedBookMetadata& fallback,
                                             const std::string& source) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  if (normalizedPath.empty()) return false;

  CachedBookMetadata metadata = fallback;
  bool foundSidecar = false;
  for (const auto& candidate : sidecarCandidatesForBook(normalizedPath)) {
    const CachedBookMetadata parsed = parseOpfFile(candidate);
    if (hasUsefulMetadata(parsed)) {
      mergeOpfOverFallback(metadata, parsed);
      foundSidecar = true;
      break;
    }
  }

  if (!hasUsefulMetadata(metadata)) {
    return false;
  }

  const std::string bookId = Storage.exists(normalizedPath.c_str()) ? BookIdentity::resolveStableBookId(normalizedPath) : "";
  return mergeMetadata(normalizedPath, bookId, metadata, foundSidecar ? source : "reader");
}

bool BookMetadataStore::importSidecarForTransferredFile(const std::string& path) {
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  if (normalizedPath.empty()) return false;

  if (isSupportedBookPath(normalizedPath)) {
    return importSidecarForBook(normalizedPath);
  }

  if (!FsHelpers::checkFileExtension(normalizedPath, ".opf")) {
    return false;
  }

  const std::string base = withoutExtension(normalizedPath);
  const std::string directCandidates[] = {base + ".epub", base + ".xtc", base + ".xtch", base + ".txt", base + ".md"};
  for (const auto& candidate : directCandidates) {
    if (Storage.exists(candidate.c_str())) {
      CachedBookMetadata parsed = parseOpfFile(normalizedPath);
      if (!hasUsefulMetadata(parsed)) return false;
      return mergeMetadata(candidate, BookIdentity::resolveStableBookId(candidate), parsed, "calibre-opf");
    }
  }

  if (basenameForPath(normalizedPath) != "metadata.opf") {
    return false;
  }

  FsFile root = Storage.open(folderForPath(normalizedPath).c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return false;
  }
  CachedBookMetadata parsed = parseOpfFile(normalizedPath);
  if (!hasUsefulMetadata(parsed)) {
    root.close();
    return false;
  }

  char name[256];
  int checked = 0;
  FsFile file = root.openNextFile();
  while (file && checked < 80) {
    checked++;
    file.getName(name, sizeof(name));
    const std::string candidate = folderForPath(normalizedPath) + name;
    const bool match = !file.isDirectory() && isSupportedBookPath(candidate);
    file.close();
    if (match) {
      root.close();
      return mergeMetadata(candidate, BookIdentity::resolveStableBookId(candidate), parsed, "calibre-opf");
    }
    file = root.openNextFile();
  }
  if (file) file.close();
  root.close();
  return false;
}

bool BookMetadataStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["version"] = BOOK_METADATA_SCHEMA_VERSION;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    if (book.path.empty() && book.bookId.empty()) {
      continue;
    }
    JsonObject item = arr.add<JsonObject>();
    if (!book.bookId.empty()) item["bookId"] = book.bookId;
    if (!book.path.empty()) item["path"] = book.path;
    if (!book.title.empty()) item["title"] = book.title;
    if (!book.author.empty()) item["author"] = book.author;
    if (!book.series.empty()) item["series"] = book.series;
    if (!book.seriesIndex.empty()) item["seriesIndex"] = book.seriesIndex;
    if (!book.tags.empty()) item["tags"] = book.tags;
    if (!book.publisher.empty()) item["publisher"] = book.publisher;
    if (!book.language.empty()) item["language"] = book.language;
    if (!book.description.empty()) item["description"] = book.description;
    if (!book.identifier.empty()) item["identifier"] = book.identifier;
    if (!book.coverPath.empty()) item["coverPath"] = book.coverPath;
    if (!book.source.empty()) item["source"] = book.source;
    item["updatedAt"] = book.updatedAt;
  }

  const std::string tempPath = std::string(BOOK_METADATA_FILE_JSON) + ".tmp";
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  HalFile file;
  if (!Storage.openFileForWrite("META", tempPath.c_str(), file)) {
    LOG_ERR("META", "Could not open book metadata temp file");
    return false;
  }
  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  if (written == 0) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (Storage.exists(BOOK_METADATA_FILE_JSON) && !Storage.remove(BOOK_METADATA_FILE_JSON)) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return Storage.rename(tempPath.c_str(), BOOK_METADATA_FILE_JSON);
}

bool BookMetadataStore::loadFromFile() {
  const std::string tempPath = std::string(BOOK_METADATA_FILE_JSON) + ".tmp";
  if (!Storage.exists(BOOK_METADATA_FILE_JSON) && Storage.exists(tempPath.c_str())) {
    Storage.rename(tempPath.c_str(), BOOK_METADATA_FILE_JSON);
  }
  if (!Storage.exists(BOOK_METADATA_FILE_JSON)) {
    loaded = true;
    return false;
  }

  const String json = Storage.readFile(BOOK_METADATA_FILE_JSON);
  if (json.isEmpty()) {
    loaded = true;
    return false;
  }

  JsonDocument doc;
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("META", "Failed to parse book metadata: %s", error.c_str());
    loaded = true;
    return false;
  }

  books.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  books.reserve(arr.size());
  for (JsonObject item : arr) {
    CachedBookMetadata book;
    book.bookId = item["bookId"] | "";
    book.path = item["path"] | "";
    book.title = item["title"] | "";
    book.author = item["author"] | "";
    book.series = item["series"] | "";
    book.seriesIndex = item["seriesIndex"] | "";
    book.tags = item["tags"] | "";
    book.publisher = item["publisher"] | "";
    book.language = item["language"] | "";
    book.description = item["description"] | "";
    book.identifier = item["identifier"] | "";
    book.coverPath = item["coverPath"] | "";
    book.source = item["source"] | "";
    book.updatedAt = item["updatedAt"] | 0;
    books.push_back(std::move(book));
  }
  normalizeBooks();
  loaded = true;
  return true;
}
