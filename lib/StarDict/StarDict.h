#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Reads uncompressed StarDict dictionaries from SD card.
//
// Format:
//   <base>.ifo  – plain-text metadata (bookname, wordcount, idxfilesize, …)
//   <base>.idx  – sorted binary index: word\0 + uint32_t offset (BE) + uint32_t size (BE)
//   <base>.dict – raw definition text, addressed by offset+size pairs from .idx
//
// Only the ".dict" variant (uncompressed) is supported; ".dict.dz" is not.
//
// Memory model: no full index is loaded into RAM.  A compact checkpoint array
// (one uint32_t per ~4 KB of .idx) is built once (see buildCheckpoints()) and
// used to narrow each lookup to a bounded linear scan.  Build checkpoints in
// DictionaryStore::scan() and pass them to every lookup() call.

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

  // Build compact checkpoint arrays for the .idx file.  Each checkpoint records
  // the byte offset of an entry boundary and the cumulative entry ordinal at
  // that boundary, sampled every ~chunkSize bytes.  Call once after open() and
  // pass both vectors to every lookup / lookupSyn call.
  bool buildCheckpoints(std::vector<uint32_t>& byteOffsets,
                        std::vector<uint32_t>& ordinals,
                        uint32_t chunkSize = 4096) const;

  // Persist both checkpoint arrays to a cache file.
  bool saveCheckpoints(const std::string& cachePath,
                       const std::vector<uint32_t>& byteOffsets,
                       const std::vector<uint32_t>& ordinals) const;

  // Load both checkpoint arrays from a cache file produced by saveCheckpoints().
  // Returns true only if the cache is valid and the stored .idx size matches.
  bool loadCheckpoints(const std::string& cachePath,
                       std::vector<uint32_t>& byteOffsets,
                       std::vector<uint32_t>& ordinals) const;

  // Look up a word (case-insensitive).  Fills `definition` (up to maxLen-1
  // bytes, always null-terminated) and returns true when found.
  // Pass the byteOffsets checkpoint array built by buildCheckpoints().
  bool lookup(const char* word, char* definition, size_t maxLen,
              const std::vector<uint32_t>* checkpoints = nullptr);

  // Look up `word` in the .syn alternate-forms file.  If found, fills
  // `outHeadword` with the canonical headword from .idx and returns true.
  // byteOffsets / ordinals are the checkpoint arrays from buildCheckpoints().
  // Returns false immediately if no .syn file exists.
  bool lookupSyn(const char* word, std::string& outHeadword,
                 const std::vector<uint32_t>& byteOffsets,
                 const std::vector<uint32_t>& ordinals) const;

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

  // Checkpoint-based lookup: binary search the in-memory checkpoint array,
  // then linear scan within the resulting ~chunkSize-byte interval.
  bool checkpointSearchIdx(HalFile& idxFile, const std::vector<uint32_t>& checkpoints,
                           const char* word, uint32_t& outOffset, uint32_t& outSize);

  // Fallback binary-search lookup (may misidentify boundaries in binary data).
  bool binarySearchIdx(HalFile& idxFile, size_t fileSize, const char* word,
                       uint32_t& outOffset, uint32_t& outSize);

  // Linear scan in [lo, hi) byte range of idxFile.
  bool linearSearchIdx(HalFile& idxFile, size_t lo, size_t hi, const char* word,
                       uint32_t& outOffset, uint32_t& outSize);

  // Binary search .syn for `word`; fills `outOrdinal` (BE uint32 .idx ordinal).
  // suffixBytes is the number of bytes after the null terminator per entry (4 for .syn).
  static bool binarySearchSyn(HalFile& synFile, size_t fileSize, const char* word,
                               uint32_t& outOrdinal);
  static bool linearSearchSyn(HalFile& synFile, size_t lo, size_t hi, const char* word,
                               uint32_t& outOrdinal);

  // Return the headword at 0-based `ordinal` in .idx using the checkpoint arrays.
  std::string headwordAtOrdinal(uint32_t ordinal,
                                const std::vector<uint32_t>& cpBytes,
                                const std::vector<uint32_t>& cpOrdinals) const;
};
