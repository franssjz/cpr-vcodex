#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "util/BookIdentity.h"

class BookmarkStore {
 public:
  struct Bookmark {
    uint16_t spineIndex = 0;
    uint16_t pageNumber = 0;
    std::string snippet;
  };

  void load(const std::string& cachePath, const std::string& bookId = "") {
    storagePath.clear();
    legacyPath.clear();
    if (!bookId.empty()) {
      BookIdentity::ensureStableDataDir(bookId);
      storagePath = BookIdentity::getStableDataFilePath(bookId, "bookmarks.bin");
      if (!cachePath.empty()) {
        legacyPath = cachePath + "/bookmarks.bin";
      }
    } else {
      storagePath = cachePath.empty() ? "" : (cachePath + "/bookmarks.bin");
    }

    bookmarks.clear();
    dirty = false;

    FsFile file;
    bool loadedLegacyPath = false;
    if (!Storage.openFileForRead("BKM", getFilePath(), file)) {
      if (storagePath == legacyPath || legacyPath.empty() || !Storage.openFileForRead("BKM", legacyPath, file)) {
        return;
      }
      loadedLegacyPath = true;
    }

    if (getFilePath().empty()) {
      return;
    }

    uint8_t version = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version < 1 ||
        version > FILE_VERSION) {
      file.close();
      return;
    }

    uint32_t count = 0;
    if (version >= 3) {
      if (file.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) != sizeof(count)) {
        file.close();
        return;
      }
    } else {
      uint16_t legacyCount = 0;
      if (file.read(reinterpret_cast<uint8_t*>(&legacyCount), sizeof(legacyCount)) != sizeof(legacyCount)) {
        file.close();
        return;
      }
      count = legacyCount;
    }

    bookmarks.reserve(static_cast<size_t>(count));
    for (uint32_t index = 0; index < count; ++index) {
      Bookmark bookmark;
      if (file.read(reinterpret_cast<uint8_t*>(&bookmark.spineIndex), sizeof(bookmark.spineIndex)) !=
              sizeof(bookmark.spineIndex) ||
          file.read(reinterpret_cast<uint8_t*>(&bookmark.pageNumber), sizeof(bookmark.pageNumber)) !=
              sizeof(bookmark.pageNumber)) {
        bookmarks.clear();
        file.close();
        return;
      }

      if (version >= 2) {
        uint8_t snippetLen = 0;
        if (file.read(&snippetLen, 1) == 1 && snippetLen > 0) {
          char buffer[MAX_SNIPPET_LEN + 1];
          const uint8_t toRead = std::min(snippetLen, static_cast<uint8_t>(MAX_SNIPPET_LEN));
          if (file.read(reinterpret_cast<uint8_t*>(buffer), toRead) == toRead) {
            buffer[toRead] = '\0';
            bookmark.snippet = buffer;
          }
          if (snippetLen > toRead) {
            file.seekCur(snippetLen - toRead);
          }
        }
      }

      bookmarks.push_back(bookmark);
    }

    file.close();

    if (loadedLegacyPath && !storagePath.empty()) {
      dirty = true;
      save();
    }
  }

  void save() {
    if (!dirty || storagePath.empty()) {
      return;
    }

    FsFile file;
    if (!Storage.openFileForWrite("BKM", getFilePath(), file)) {
      LOG_ERR("BKM", "Failed to save bookmarks");
      return;
    }

    auto writePodChecked = [&file](const auto& value) {
      return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
    };

    const uint32_t count = static_cast<uint32_t>(bookmarks.size());
    bool ok = writePodChecked(FILE_VERSION) && writePodChecked(count);

    for (const auto& bookmark : bookmarks) {
      ok = ok && writePodChecked(bookmark.spineIndex) && writePodChecked(bookmark.pageNumber);
      const uint8_t snippetLen =
          static_cast<uint8_t>(std::min(bookmark.snippet.size(), static_cast<size_t>(MAX_SNIPPET_LEN)));
      ok = ok && writePodChecked(snippetLen);
      if (snippetLen > 0) {
        ok = ok && file.write(reinterpret_cast<const uint8_t*>(bookmark.snippet.c_str()), snippetLen) == snippetLen;
      }
    }

    ok = ok && file.close();
    if (!ok) {
      LOG_ERR("BKM", "Failed while writing bookmarks");
      return;
    }

    dirty = false;
  }

  bool toggle(const uint16_t spineIndex, const uint16_t pageNumber, const std::string& snippet = "") {
    auto it = find(spineIndex, pageNumber);
    if (it != bookmarks.end()) {
      bookmarks.erase(it);
      dirty = true;
      return false;
    }

    bookmarks.push_back({spineIndex, pageNumber, snippet.substr(0, MAX_SNIPPET_LEN)});
    dirty = true;
    return true;
  }

  bool remove(const uint16_t spineIndex, const uint16_t pageNumber) {
    auto it = find(spineIndex, pageNumber);
    if (it == bookmarks.end()) {
      return false;
    }

    bookmarks.erase(it);
    dirty = true;
    return true;
  }

  void clear() {
    if (bookmarks.empty()) {
      return;
    }
    bookmarks.clear();
    dirty = true;
  }

  [[nodiscard]] bool has(const uint16_t spineIndex, const uint16_t pageNumber) const {
    return std::any_of(bookmarks.begin(), bookmarks.end(), [spineIndex, pageNumber](const Bookmark& bookmark) {
      return bookmark.spineIndex == spineIndex && bookmark.pageNumber == pageNumber;
    });
  }

  [[nodiscard]] const std::vector<Bookmark>& getAll() const { return bookmarks; }
  [[nodiscard]] bool isEmpty() const { return bookmarks.empty(); }
  void markDirty() { dirty = true; }

 private:
  static constexpr uint8_t FILE_VERSION = 3;
  static constexpr uint8_t MAX_SNIPPET_LEN = 80;

  std::vector<Bookmark> bookmarks;
  std::string storagePath;
  std::string legacyPath;
  bool dirty = false;

  [[nodiscard]] std::string getFilePath() const { return storagePath; }

  std::vector<Bookmark>::iterator find(const uint16_t spineIndex, const uint16_t pageNumber) {
    return std::find_if(bookmarks.begin(), bookmarks.end(), [spineIndex, pageNumber](const Bookmark& bookmark) {
      return bookmark.spineIndex == spineIndex && bookmark.pageNumber == pageNumber;
    });
  }
};
