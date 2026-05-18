#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CachedBookMetadata {
  std::string bookId;
  std::string path;
  std::string title;
  std::string author;
  std::string series;
  std::string seriesIndex;
  std::string tags;
  std::string publisher;
  std::string language;
  std::string description;
  std::string identifier;
  std::string coverPath;
  std::string source;
  uint32_t updatedAt = 0;
};

class BookMetadataStore {
  static BookMetadataStore instance;

  std::vector<CachedBookMetadata> books;
  bool loaded = false;

  void ensureLoaded();
  int findBookIndex(const std::string& path, const std::string& bookId) const;
  CachedBookMetadata& getOrCreateBook(const std::string& path, const std::string& bookId = "");
  void normalizeBook(CachedBookMetadata& book) const;
  void normalizeBooks();

 public:
  static BookMetadataStore& getInstance() { return instance; }

  const CachedBookMetadata* findBook(const std::string& path, const std::string& bookId = "");
  bool mergeMetadata(const std::string& path, const std::string& bookId, const CachedBookMetadata& metadata,
                     const std::string& source);
  bool importSidecarForBook(const std::string& path, const CachedBookMetadata& fallback = {},
                            const std::string& source = "calibre-opf");
  bool importSidecarForTransferredFile(const std::string& path);
  bool saveToFile() const;
  bool loadFromFile();
};

#define BOOK_METADATA BookMetadataStore::getInstance()
