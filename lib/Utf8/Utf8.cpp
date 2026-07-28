#include "Utf8.h"

#include "Utf8ComposeTable.h"

namespace {
// Look up the canonical composition of (base + combining mark), or 0 if none.
uint32_t utf8ComposePair(const uint32_t base, const uint32_t mark) {
  if (base > 0xFFFF || mark > 0xFFFF) return 0;
  int lo = 0;
  int hi = kUtf8ComposeTableSize - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const Utf8ComposeEntry& e = kUtf8ComposeTable[mid];
    if (e.base < base || (e.base == base && e.mark < mark)) {
      lo = mid + 1;
    } else if (e.base > base || (e.base == base && e.mark > mark)) {
      hi = mid - 1;
    } else {
      return e.composed;
    }
  }
  return 0;
}
}  // namespace

std::string utf8ComposeNfc(const std::string& in) {
  // Fast path: NFC composition can only change text that contains a combining
  // diacritical mark U+0300-036F (UTF-8 lead byte 0xCC or 0xCD). Plain ASCII and
  // already-precomposed (NFC) text -- the vast majority of words -- have none, so
  // return them untouched without walking codepoints or allocating. A 0xCD that is
  // actually a non-combining codepoint just falls through to the full pass below.
  bool maybeHasMarks = false;
  for (const unsigned char c : in) {
    if (c == 0xCC || c == 0xCD) {
      maybeHasMarks = true;
      break;
    }
  }
  if (!maybeHasMarks) return in;

  std::string out;
  out.reserve(in.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in.c_str());
  uint32_t base = 0;
  bool haveBase = false;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (utf8IsCombiningMark(cp)) {
      const uint32_t composed = haveBase ? utf8ComposePair(base, cp) : 0;
      if (composed) {
        base = composed;  // keep accumulating further marks onto the composed char
        continue;
      }
      // No composition: flush the pending base, then emit the mark unchanged.
      if (haveBase) {
        utf8AppendCodepoint(base, out);
        haveBase = false;
      }
      utf8AppendCodepoint(cp, out);
    } else {
      if (haveBase) utf8AppendCodepoint(base, out);
      base = cp;
      haveBase = true;
    }
  }
  if (haveBase) utf8AppendCodepoint(base, out);
  return out;
}

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const unsigned char lead = **string;
  const int bytes = utf8CodepointLen(lead);
  const uint8_t* chr = *string;

  // Invalid lead byte (stray continuation byte 0x80-0xBF, or 0xFE/0xFF)
  if (bytes == 1 && lead >= 0x80) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  if (bytes == 1) {
    (*string)++;
    return chr[0];
  }

  // Validate continuation bytes before consuming them
  for (int i = 1; i < bytes; i++) {
    if ((chr[i] & 0xC0) != 0x80) {
      // Missing or invalid continuation byte — skip all bytes consumed so far
      *string += i;
      return REPLACEMENT_GLYPH;
    }
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  // Reject overlong encodings, surrogates, and out-of-range values
  const bool overlong = (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
  const bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
  if (overlong || surrogate || cp > 0x10FFFF) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  *string += bytes;

  return cp;
}

void utf8AppendCodepoint(const uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

int utf8SafeTruncateBuffer(const char* buf, int len) {
  if (len <= 0) return 0;

  // Walk back past continuation bytes (10xxxxxx) to find the lead byte
  int leadPos = len - 1;
  while (leadPos > 0 && (static_cast<uint8_t>(buf[leadPos]) & 0xC0) == 0x80) {
    leadPos--;
  }

  // Determine expected length of the sequence starting at leadPos
  int expectedLen = utf8CodepointLen(static_cast<unsigned char>(buf[leadPos]));
  int actualLen = len - leadPos;

  if (actualLen < expectedLen && leadPos > 0) {
    // Incomplete UTF-8 sequence at the end — exclude it
    return leadPos;
  }
  return len;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

// Truncate string by removing N UTF-8 characters from the end
void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

uint32_t utf8CountLayoutWords(const char* data, const size_t len) {
  if (!data || len == 0) {
    return 0;
  }

  // Mirror ChapterHtmlSlimParser characterData: accumulate a run, flush at
  // UTF8_LAYOUT_WORD_MAX_BYTES with utf8SafeTruncateBuffer (never mid-sequence).
  uint32_t words = 0;
  char run[UTF8_LAYOUT_WORD_MAX_BYTES + 4] = {};
  int runLen = 0;

  auto flushRun = [&]() {
    if (runLen <= 0) {
      return;
    }
    ++words;
    runLen = 0;
  };

  auto flushAtCapacity = [&]() {
    if (runLen < static_cast<int>(UTF8_LAYOUT_WORD_MAX_BYTES)) {
      return;
    }
    const int safeLen = utf8SafeTruncateBuffer(run, runLen);
    if (safeLen <= 0) {
      runLen = 0;
      return;
    }
    if (safeLen < runLen) {
      const int overflow = runLen - safeLen;
      char saved[4];
      for (int j = 0; j < overflow && j < 4; ++j) {
        saved[j] = run[safeLen + j];
      }
      runLen = safeLen;
      flushRun();
      for (int j = 0; j < overflow && j < 4; ++j) {
        run[j] = saved[j];
      }
      runLen = overflow;
    } else {
      flushRun();
    }
  };

  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
      flushRun();
      continue;
    }

    // U+00A0 (C2 A0) — own layout word, like EPUB.
    if (c == 0xC2 && i + 1 < len && static_cast<unsigned char>(data[i + 1]) == 0xA0) {
      flushRun();
      ++words;
      ++i;
      continue;
    }

    // U+202F (E2 80 AF) — narrow no-break space.
    if (c == 0xE2 && i + 2 < len && static_cast<unsigned char>(data[i + 1]) == 0x80 &&
        static_cast<unsigned char>(data[i + 2]) == 0xAF) {
      flushRun();
      ++words;
      i += 2;
      continue;
    }

    // U+FEFF BOM / ZWNBSP — skip.
    if (c == 0xEF && i + 2 < len && static_cast<unsigned char>(data[i + 1]) == 0xBB &&
        static_cast<unsigned char>(data[i + 2]) == 0xBF) {
      i += 2;
      continue;
    }

    flushAtCapacity();
    if (runLen < static_cast<int>(sizeof(run))) {
      run[runLen++] = static_cast<char>(c);
    }
  }
  flushRun();
  return words;
}
