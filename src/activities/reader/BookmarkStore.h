#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Stores per-book bookmarks inside the reader cache directory.
// Bookmarks are saved with deferred writes to reduce SD I/O jank (60-second interval).
// This prevents bookmark toggle operations (30-50 ms each) from blocking the UI when
// multiple bookmarks are toggled rapidly.
class BookmarkStore {
 public:
  struct Bookmark {
    uint16_t spineIndex = 0;
    uint16_t pageNumber = 0;
    std::string snippet;
  };

  void load(const std::string& cachePath) {
    basePath = cachePath;
    bookmarks.clear();
    dirty = false;

    FsFile file;
    if (!Storage.openFileForRead("BKM", getFilePath(), file)) {
      return;
    }

    uint8_t version = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version < 1 ||
        version > FILE_VERSION) {
      file.close();
      return;
    }

    uint16_t count = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) != sizeof(count) || count > MAX_BOOKMARKS) {
      file.close();
      return;
    }

    bookmarks.reserve(count);
    for (uint16_t index = 0; index < count; index++) {
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
    lastSaveTime = millis();
  }

  /**
   * @brief Immediately save all bookmarks to disk
   * Used during activity exit to ensure no data loss
   */
  void save() {
    if (!dirty || basePath.empty()) {
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

    const uint16_t count = static_cast<uint16_t>(bookmarks.size());
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
    lastSaveTime = millis();
  }

  /**
   * @brief Deferred save: only write to disk if dirty AND 60+ seconds has elapsed
   * Call this frequently from the reader loop (e.g., every frame).
   * This eliminates I/O jank when bookmarks are toggled rapidly.
   * @return true if a save occurred, false if deferred or not needed
   */
  bool saveIfDeferredIntervalElapsed() {
    if (!dirty || basePath.empty()) {
      return false;
    }

    const unsigned long now = millis();
    if (now - lastSaveTime < DEFERRED_SAVE_INTERVAL_MS) {
      return false;  // Too soon, defer this save
    }

    save();  // This will update lastSaveTime
    return true;
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
  [[nodiscard]] bool isDirty() const { return dirty; }

 private:
  static constexpr uint8_t FILE_VERSION = 2;
  static constexpr uint16_t MAX_BOOKMARKS = 1000;
  static constexpr uint8_t MAX_SNIPPET_LEN = 80;
  /// Deferred save interval: only write to disk if 60+ seconds since last save and dirty
  /// This prevents I/O jank when multiple bookmarks are toggled rapidly (<1 sec apart)
  static constexpr unsigned long DEFERRED_SAVE_INTERVAL_MS = 60UL * 1000UL;

  std::vector<Bookmark> bookmarks;
  std::string basePath;
  bool dirty = false;
  unsigned long lastSaveTime = 0;

  [[nodiscard]] std::string getFilePath() const { return basePath + "/bookmarks.bin"; }

  std::vector<Bookmark>::iterator find(const uint16_t spineIndex, const uint16_t pageNumber) {
    return std::find_if(bookmarks.begin(), bookmarks.end(), [spineIndex, pageNumber](const Bookmark& bookmark) {
      return bookmark.spineIndex == spineIndex && bookmark.pageNumber == pageNumber;
    });
  }
};
