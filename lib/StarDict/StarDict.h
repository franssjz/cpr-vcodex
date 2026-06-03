#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Reads uncompressed StarDict dictionaries from SD card.
//
// Format:
//   <base>.ifo  – plain-text metadata (bookname, wordcount, idxfilesize, …)
//   <base>.idx  – sorted binary index: word\0 + uint32_t offset (BE) + uint32_t size (BE)
//   <base>.dict – raw definition text, addressed by offset+size pairs from .idx
//
// Only the ".dict" variant (uncompressed) is supported; ".dict.dz" is not.
//
// Memory model: no index is loaded into RAM. Every lookup performs a binary
// search by seeking directly in the .idx file. Keep this in mind – each call
// to lookup() issues multiple SD reads.

class StarDict {
 public:
  StarDict() = default;
  ~StarDict() { close(); }

  StarDict(const StarDict&) = delete;
  StarDict& operator=(const StarDict&) = delete;

  // Open a dictionary given the path to the .ifo file.
  // Returns true on success.
  bool open(const std::string& ifoPath);

  // Close all file handles.
  void close();

  bool isOpen() const { return opened; }

  // Human-readable dictionary name from the .ifo bookname field.
  const std::string& getName() const { return bookName; }

  // Language tag from the .ifo lang field (may be empty).
  const std::string& getLanguage() const { return language; }

  // Total number of words (informational only).
  uint32_t getWordCount() const { return wordCount; }

  // Look up a word (case-insensitive).  Fills `definition` (up to maxLen-1
  // bytes, always null-terminated) and returns true when found.
  // Pass a stack buffer of at least 512 bytes for `definition`.
  bool lookup(const char* word, char* definition, size_t maxLen);

 private:
  static constexpr size_t MAX_WORD_LEN = 256;
  // When the remaining search range (bytes) drops below this, switch to a
  // linear scan so we do not loop forever on very small files or edge cases.
  static constexpr size_t LINEAR_SCAN_THRESHOLD = 1024;

  std::string idxPath;
  std::string dictPath;
  std::string bookName;
  std::string language;
  uint32_t wordCount = 0;
  size_t idxFileSize = 0;
  bool opened = false;

  // Parse one line from the .ifo text file and extract the value.
  static bool parseIfoField(const char* line, const char* key, std::string& out);

  // Read a big-endian uint32_t from 4 bytes.
  static uint32_t readBE32(const uint8_t* buf);

  // Case-insensitive character comparison helper.
  static int wordcmp(const char* a, const char* b);

  // Read at most maxLen-1 bytes of a null-terminated word from `file` at its
  // current position. Returns the number of bytes written (excluding null).
  // After return the file position is right after the null terminator.
  static int readWordFromFile(HalFile& file, char* buf, int maxLen);

  // Perform the binary-search lookup. Returns true on match; fills *outOffset
  // and *outSize.
  bool binarySearchIdx(HalFile& idxFile, size_t fileSize, const char* word,
                       uint32_t& outOffset, uint32_t& outSize);

  // Linear scan in [lo, hi) byte range of idxFile.
  bool linearSearchIdx(HalFile& idxFile, size_t lo, size_t hi, const char* word,
                       uint32_t& outOffset, uint32_t& outSize);
};
