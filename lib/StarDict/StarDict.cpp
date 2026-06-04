#include "StarDict.h"

#include <FsHelpers.h>
#include <Logging.h>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Strip trailing carriage-return / newline from a mutable C-string.
void stripCRLF(char* s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
    s[len - 1] = '\0';
    --len;
  }
}

// Read one line (up to maxLen-1 bytes) from file into buf.
// Returns false on EOF/error.
bool readLine(HalFile& file, char* buf, int maxLen) {
  int i = 0;
  while (i < maxLen - 1) {
    int c = file.read();
    if (c < 0) {
      break;  // EOF or error
    }
    buf[i++] = static_cast<char>(c);
    if (c == '\n') {
      break;
    }
  }
  buf[i] = '\0';
  return i > 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

bool StarDict::parseIfoField(const char* line, const char* key, std::string& out) {
  const size_t klen = strlen(key);
  if (strncmp(line, key, klen) != 0 || line[klen] != '=') {
    return false;
  }
  const char* val = line + klen + 1;
  out = val;
  // Remove trailing whitespace
  while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ')) {
    out.pop_back();
  }
  return true;
}

uint32_t StarDict::readBE32(const uint8_t* buf) {
  return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

int StarDict::wordcmp(const char* a, const char* b) {
  while (*a && *b) {
    const int ca = tolower(static_cast<unsigned char>(*a));
    const int cb = tolower(static_cast<unsigned char>(*b));
    if (ca != cb) {
      return ca - cb;
    }
    ++a;
    ++b;
  }
  return tolower(static_cast<unsigned char>(*a)) - tolower(static_cast<unsigned char>(*b));
}

int StarDict::readWordFromFile(HalFile& file, char* buf, int maxLen) {
  int count = 0;
  while (count < maxLen - 1) {
    int c = file.read();
    if (c < 0) {
      break;  // EOF
    }
    if (c == '\0') {
      break;
    }
    buf[count++] = static_cast<char>(c);
  }
  buf[count] = '\0';
  return count;
}

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

bool StarDict::open(const std::string& ifoPath) {
  close();

  // 1. Read .ifo metadata
  HalFile ifoFile;
  if (!Storage.openFileForRead("SD", ifoPath, ifoFile)) {
    LOG_ERR("SD", "StarDict: cannot open .ifo: %s", ifoPath.c_str());
    return false;
  }

  char lineBuf[256];
  // First line must be the magic header
  if (!readLine(ifoFile, lineBuf, sizeof(lineBuf))) {
    return false;
  }
  stripCRLF(lineBuf);
  if (strcmp(lineBuf, "StarDict's dict ifo file") != 0) {
    LOG_ERR("SD", "StarDict: bad .ifo magic in %s", ifoPath.c_str());
    return false;
  }

  uint32_t fileIdxSize = 0;
  bool gotVersion = false;
  while (readLine(ifoFile, lineBuf, sizeof(lineBuf))) {
    stripCRLF(lineBuf);
    std::string val;
    if (parseIfoField(lineBuf, "version", val)) {
      if (val != "2.4.2" && val != "3.0.0") {
        LOG_ERR("SD", "StarDict: unsupported version '%s' in %s", val.c_str(), ifoPath.c_str());
        return false;
      }
      gotVersion = true;
    } else if (parseIfoField(lineBuf, "wordcount", val)) {
      wordCount = static_cast<uint32_t>(atol(val.c_str()));
    } else if (parseIfoField(lineBuf, "idxfilesize", val)) {
      fileIdxSize = static_cast<uint32_t>(atol(val.c_str()));
    } else if (parseIfoField(lineBuf, "bookname", val)) {
      bookName = val;
    } else if (parseIfoField(lineBuf, "lang", val)) {
      language = val;
    }
  }
  ifoFile.close();

  if (!gotVersion) {
    LOG_ERR("SD", "StarDict: missing version in .ifo: %s", ifoPath.c_str());
    return false;
  }

  // 2. Derive sibling file paths
  const std::string base = ifoPath.substr(0, ifoPath.size() - 4);  // strip ".ifo"
  idxPath = base + ".idx";
  dictPath = base + ".dict";

  if (!Storage.exists(idxPath.c_str())) {
    LOG_ERR("SD", "StarDict: .idx not found: %s", idxPath.c_str());
    return false;
  }
  if (!Storage.exists(dictPath.c_str())) {
    LOG_ERR("SD", "StarDict: .dict not found: %s", dictPath.c_str());
    return false;
  }

  // Verify idx file size if reported
  if (fileIdxSize > 0) {
    HalFile tmpIdx;
    if (Storage.openFileForRead("SD", idxPath, tmpIdx)) {
      idxFileSize = tmpIdx.fileSize();
      tmpIdx.close();
    }
  } else {
    HalFile tmpIdx;
    if (Storage.openFileForRead("SD", idxPath, tmpIdx)) {
      idxFileSize = tmpIdx.fileSize();
      tmpIdx.close();
    }
  }

  if (idxFileSize == 0) {
    LOG_ERR("SD", "StarDict: empty .idx: %s", idxPath.c_str());
    return false;
  }

  if (bookName.empty()) {
    // Fall back to filename stem as name
    const size_t slash = ifoPath.find_last_of('/');
    const std::string stem = (slash != std::string::npos) ? ifoPath.substr(slash + 1) : ifoPath;
    bookName = stem.substr(0, stem.size() - 4);
  }

  LOG_DBG("SD", "StarDict: opened '%s' (%u words)", bookName.c_str(), wordCount);
  opened = true;
  return true;
}

void StarDict::close() {
  idxPath.clear();
  dictPath.clear();
  bookName.clear();
  language.clear();
  wordCount = 0;
  idxFileSize = 0;
  opened = false;
}

// ---------------------------------------------------------------------------
// buildCheckpoints
// ---------------------------------------------------------------------------

bool StarDict::buildCheckpoints(std::vector<uint32_t>& out, uint32_t chunkSize) const {
  out.clear();
  if (!opened) {
    return false;
  }

  HalFile idxFile;
  if (!Storage.openFileForRead("SD", idxPath, idxFile)) {
    LOG_ERR("SD", "StarDict::buildCheckpoints: cannot open idx: %s", idxPath.c_str());
    return false;
  }

  // First entry always starts at byte 0.
  out.push_back(0);
  uint32_t nextCheckpoint = chunkSize;

  // Read in blocks for efficiency; parse the structure byte-by-byte.
  // Because we start from position 0 (a known-good boundary), every null
  // byte we encounter in `inWord=true` state is guaranteed to be a real
  // word terminator, not a false positive from binary header data.
  static constexpr uint32_t BLOCK_SIZE = 512;
  uint8_t block[BLOCK_SIZE];
  uint32_t filePos = 0;
  bool inWord = true;     // true: reading word chars; false: consuming 8-byte binary header
  uint8_t binaryLeft = 0; // header bytes remaining

  while (true) {
    const int n = idxFile.read(block, BLOCK_SIZE);
    if (n <= 0) {
      break;
    }
    for (int i = 0; i < n; ++i) {
      if (inWord) {
        if (block[i] == '\0') {
          inWord = false;
          binaryLeft = 8;
        }
      } else {
        if (--binaryLeft == 0) {
          // byte at filePos+i was the last header byte; next byte is entry start.
          const uint32_t entryStart = filePos + static_cast<uint32_t>(i) + 1;
          if (entryStart >= nextCheckpoint) {
            out.push_back(entryStart);
            nextCheckpoint = entryStart + chunkSize;
          }
          inWord = true;
        }
      }
    }
    filePos += static_cast<uint32_t>(n);
  }

  idxFile.close();
  LOG_DBG("SD", "StarDict: built %u checkpoints for '%s'",
          static_cast<unsigned>(out.size()), bookName.c_str());
  return true;
}

// ---------------------------------------------------------------------------
// saveCheckpoints / loadCheckpoints
// ---------------------------------------------------------------------------
// Cache file format (all integers big-endian uint32):
//   [0..3]  magic  = 0x44 0x43 0x48 0x4B  ("DCHK")
//   [4..7]  idxFileSize
//   [8..11] checkpoint count N
//   [12..]  N × checkpoint values

static constexpr uint8_t CACHE_MAGIC[4] = {0x44, 0x43, 0x48, 0x4B};

static void writeBE32(uint8_t* buf, uint32_t v) {
  buf[0] = static_cast<uint8_t>(v >> 24);
  buf[1] = static_cast<uint8_t>(v >> 16);
  buf[2] = static_cast<uint8_t>(v >> 8);
  buf[3] = static_cast<uint8_t>(v);
}

bool StarDict::saveCheckpoints(const std::string& cachePath,
                                const std::vector<uint32_t>& checkpoints) const {
  if (!opened || checkpoints.empty()) {
    return false;
  }

  // Write to a temp file then rename for atomicity.
  const std::string tmpPath = cachePath + ".tmp";
  Storage.remove(tmpPath.c_str());

  HalFile f;
  if (!Storage.openFileForWrite("SD", tmpPath, f)) {
    LOG_ERR("SD", "StarDict: cannot write checkpoint cache: %s", tmpPath.c_str());
    return false;
  }

  // Header: magic + idxFileSize + count
  uint8_t hdr[12];
  hdr[0] = CACHE_MAGIC[0];
  hdr[1] = CACHE_MAGIC[1];
  hdr[2] = CACHE_MAGIC[2];
  hdr[3] = CACHE_MAGIC[3];
  writeBE32(hdr + 4, static_cast<uint32_t>(idxFileSize));
  writeBE32(hdr + 8, static_cast<uint32_t>(checkpoints.size()));
  if (f.write(hdr, sizeof(hdr)) != sizeof(hdr)) {
    f.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  // Checkpoint values
  uint8_t buf[4];
  for (const uint32_t cp : checkpoints) {
    writeBE32(buf, cp);
    if (f.write(buf, 4) != 4) {
      f.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  f.flush();
  f.close();

  Storage.remove(cachePath.c_str());
  if (!Storage.rename(tmpPath.c_str(), cachePath.c_str())) {
    LOG_ERR("SD", "StarDict: checkpoint cache rename failed: %s", cachePath.c_str());
    return false;
  }

  LOG_DBG("SD", "StarDict: saved %u checkpoints to '%s'",
          static_cast<unsigned>(checkpoints.size()), cachePath.c_str());
  return true;
}

bool StarDict::loadCheckpoints(const std::string& cachePath,
                                std::vector<uint32_t>& out) const {
  out.clear();
  if (!opened) {
    return false;
  }
  if (!Storage.exists(cachePath.c_str())) {
    return false;
  }

  HalFile f;
  if (!Storage.openFileForRead("SD", cachePath, f)) {
    return false;
  }

  uint8_t hdr[12];
  if (f.read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) {
    f.close();
    return false;
  }

  // Validate magic
  if (hdr[0] != CACHE_MAGIC[0] || hdr[1] != CACHE_MAGIC[1] ||
      hdr[2] != CACHE_MAGIC[2] || hdr[3] != CACHE_MAGIC[3]) {
    f.close();
    return false;
  }

  // Validate idx size matches current file
  const uint32_t cachedSize = readBE32(hdr + 4);
  if (cachedSize != static_cast<uint32_t>(idxFileSize)) {
    f.close();
    LOG_DBG("SD", "StarDict: checkpoint cache stale (idx size %u vs %u), rebuilding",
            cachedSize, static_cast<uint32_t>(idxFileSize));
    return false;
  }

  const uint32_t count = readBE32(hdr + 8);
  if (count == 0 || count > 65536) {  // sanity cap
    f.close();
    return false;
  }

  out.reserve(count);
  uint8_t buf[4];
  for (uint32_t i = 0; i < count; ++i) {
    if (f.read(buf, 4) != 4) {
      out.clear();
      f.close();
      return false;
    }
    out.push_back(readBE32(buf));
  }

  f.close();
  LOG_DBG("SD", "StarDict: loaded %u checkpoints from cache '%s'",
          static_cast<unsigned>(out.size()), cachePath.c_str());
  return true;
}

// ---------------------------------------------------------------------------
// lookup
// ---------------------------------------------------------------------------

bool StarDict::lookup(const char* word, char* definition, size_t maxLen,
                      const std::vector<uint32_t>* checkpoints) {
  if (!opened || !word || !definition || maxLen == 0) {
    return false;
  }
  definition[0] = '\0';

  HalFile idxFile;
  if (!Storage.openFileForRead("SD", idxPath, idxFile)) {
    LOG_ERR("SD", "StarDict: cannot open idx for lookup: %s", idxPath.c_str());
    return false;
  }

  uint32_t offset = 0;
  uint32_t size = 0;
  bool found;
  if (checkpoints != nullptr && !checkpoints->empty()) {
    found = checkpointSearchIdx(idxFile, *checkpoints, word, offset, size);
  } else {
    found = binarySearchIdx(idxFile, idxFileSize, word, offset, size);
  }
  idxFile.close();

  if (!found || size == 0) {
    return false;
  }

  // Cap definition read to available buffer
  const size_t readLen = (size < maxLen - 1) ? size : (maxLen - 1);

  HalFile dictFile;
  if (!Storage.openFileForRead("SD", dictPath, dictFile)) {
    LOG_ERR("SD", "StarDict: cannot open dict for read: %s", dictPath.c_str());
    return false;
  }
  if (!dictFile.seek(offset)) {
    dictFile.close();
    return false;
  }
  const int bytesRead = dictFile.read(definition, readLen);
  dictFile.close();

  if (bytesRead <= 0) {
    definition[0] = '\0';
    return false;
  }
  definition[bytesRead] = '\0';
  return true;
}

// ---------------------------------------------------------------------------
// checkpointSearchIdx
// ---------------------------------------------------------------------------

bool StarDict::checkpointSearchIdx(HalFile& idxFile, const std::vector<uint32_t>& checkpoints,
                                   const char* word, uint32_t& outOffset, uint32_t& outSize) {
  // Binary search the in-memory checkpoint array to find the largest
  // checkpoint index whose entry word is <= the target word.
  size_t lo = 0;
  size_t hi = checkpoints.size();  // exclusive
  char buf[MAX_WORD_LEN];

  while (lo + 1 < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (!idxFile.seek(checkpoints[mid])) {
      break;  // seek failure: fall back to scanning from lo
    }
    const int wlen = readWordFromFile(idxFile, buf, static_cast<int>(MAX_WORD_LEN));
    if (wlen == 0) {
      break;
    }
    if (wordcmp(word, buf) < 0) {
      hi = mid;  // target is before this checkpoint
    } else {
      lo = mid;  // target is at or after this checkpoint
    }
  }

  // Linear scan within [checkpoints[lo], checkpoints[lo+1]) or end of file.
  const uint32_t scanLo = checkpoints[lo];
  const uint32_t scanHi = (lo + 1 < checkpoints.size())
                              ? checkpoints[lo + 1]
                              : static_cast<uint32_t>(idxFileSize);

  return linearSearchIdx(idxFile, scanLo, scanHi, word, outOffset, outSize);
}

// ---------------------------------------------------------------------------
// binarySearchIdx
// ---------------------------------------------------------------------------

bool StarDict::binarySearchIdx(HalFile& idxFile, size_t fileSize, const char* word,
                                uint32_t& outOffset, uint32_t& outSize) {
  size_t lo = 0;
  size_t hi = fileSize;

  char wordBuf[MAX_WORD_LEN];

  while (hi > lo && (hi - lo) > LINEAR_SCAN_THRESHOLD) {
    const size_t mid = lo + (hi - lo) / 2;

    // Seek slightly BEFORE mid so we never land inside the 8-byte binary
    // offset/size field of an entry.  Those bytes can contain 0x00 values
    // (e.g. offset 0x00001234 starts with two 0x00 bytes), which would be
    // mistaken for a word-terminator null if we started scanning from mid
    // directly.  By starting MAX_WORD_LEN+8 bytes earlier we guarantee we
    // encounter the real null terminator of a word before reaching mid.
    //
    // Correctness: when hi-lo > LINEAR_SCAN_THRESHOLD (1024), mid equals
    // lo + (hi-lo)/2 >= lo + 512, so safeScanFrom = mid - 264 >= lo + 248 > lo.
    // entryStart after the scan is therefore always in (lo, hi).
    static constexpr size_t SCAN_BACK = MAX_WORD_LEN + 8;  // 264 bytes
    const size_t safeScanFrom =
        (mid > SCAN_BACK) ? (mid - SCAN_BACK) : 0;
    if (!idxFile.seek(safeScanFrom)) {
      break;
    }

    // Skip forward past the current (partial) word by scanning for null.
    {
      int limit = static_cast<int>(MAX_WORD_LEN) + 1;
      while (limit-- > 0) {
        int c = idxFile.read();
        if (c < 0) {
          goto doLinear;  // EOF inside word – fall back
        }
        if (c == '\0') {
          break;
        }
      }
      if (limit < 0) {
        goto doLinear;  // No null found within MAX_WORD_LEN – malformed file
      }
    }

    // Skip the 8-byte offset+size of the entry whose word we just skipped.
    if (!idxFile.seekCur(8)) {
      break;
    }

    {
      const size_t entryStart = idxFile.position();

      if (entryStart >= hi) {
        // We skipped past hi; the match (if any) is in [lo, mid).
        hi = mid;
        continue;
      }

      // Read the word of this next entry.
      const int wlen = readWordFromFile(idxFile, wordBuf, static_cast<int>(MAX_WORD_LEN));
      if (wlen == 0) {
        goto doLinear;
      }

      const int cmp = wordcmp(word, wordBuf);
      if (cmp == 0) {
        // Exact (case-insensitive) match on this entry.
        uint8_t hdr[8];
        if (idxFile.read(hdr, 8) != 8) {
          return false;
        }
        outOffset = readBE32(hdr);
        outSize = readBE32(hdr + 4);
        return true;
      } else if (cmp < 0) {
        // Target comes before this entry.
        hi = entryStart;
      } else {
        // Target comes after this entry.
        lo = entryStart + static_cast<size_t>(wlen) + 1 + 8;
      }
    }
  }

doLinear:
  return linearSearchIdx(idxFile, lo, hi, word, outOffset, outSize);
}

// ---------------------------------------------------------------------------
// linearSearchIdx
// ---------------------------------------------------------------------------

bool StarDict::linearSearchIdx(HalFile& idxFile, size_t lo, size_t hi, const char* word,
                                uint32_t& outOffset, uint32_t& outSize) {
  if (lo >= hi) {
    return false;
  }
  if (!idxFile.seek(lo)) {
    return false;
  }

  char wordBuf[MAX_WORD_LEN];

  while (idxFile.position() < hi) {
    const int wlen = readWordFromFile(idxFile, wordBuf, static_cast<int>(MAX_WORD_LEN));
    if (wlen == 0 && idxFile.position() >= hi) {
      break;
    }

    uint8_t hdr[8];
    if (idxFile.read(hdr, 8) != 8) {
      break;
    }

    const int cmp = wordcmp(word, wordBuf);
    if (cmp == 0) {
      outOffset = readBE32(hdr);
      outSize = readBE32(hdr + 4);
      return true;
    }
    // Note: we intentionally do NOT break early when cmp < 0 here.
    // The binary search may have advanced lo slightly past the target if it
    // misread an entry boundary (the fix in binarySearchIdx handles the main
    // cause, but this provides a second safety net by always scanning the
    // full [lo, hi] window rather than stopping prematurely).
  }
  return false;
}
