#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LibraryBookMetadata {
  std::string bookId;
  std::string path;
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

  const std::vector<LibraryBookMetadata>& getBooks() const { return books; }

  bool saveToFile() const;
  bool loadFromFile();
};

#define LIBRARY_METADATA LibraryMetadataStore::getInstance()
