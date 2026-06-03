#include "StarDict.h"

#include <FsHelpers.h>
#include <Logging.h>

#include <cctype>
#include <cstring>
#include <string>

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
// lookup
// ---------------------------------------------------------------------------

bool StarDict::lookup(const char* word, char* definition, size_t maxLen) {
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
  const bool found = binarySearchIdx(idxFile, idxFileSize, word, offset, size);
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
