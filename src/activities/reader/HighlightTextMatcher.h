#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HighlightTextMatcher {

enum class TokenFragmentMatch : uint8_t {
  MISMATCH,
  CONTINUES_TOKEN,
  COMPLETES_TOKEN,
};

struct TokenFragmentResult {
  TokenFragmentMatch match = TokenFragmentMatch::MISMATCH;
  size_t tokenBytes = 0;
};

inline bool isNonBreakingSpace(const char* text, const size_t length, const size_t offset) {
  return offset + 1 < length && static_cast<unsigned char>(text[offset]) == 0xC2 &&
         static_cast<unsigned char>(text[offset + 1]) == 0xA0;
}

inline bool isUtf8Ellipsis(const char* text) {
  return text && static_cast<unsigned char>(text[0]) == 0xE2 && static_cast<unsigned char>(text[1]) == 0x80 &&
         static_cast<unsigned char>(text[2]) == 0xA6;
}

inline bool nextToken(const char*& cursor, const char*& start, size_t& length) {
  while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (!*cursor) return false;
  start = cursor;
  while (*cursor) {
    if (!std::isspace(static_cast<unsigned char>(*cursor))) {
      ++cursor;
      continue;
    }
    // Layout can split "word …" into adjacent rendered fragments where the
    // second starts with NBSP. Preserve that separator inside this token.
    if (*cursor == ' ' && isUtf8Ellipsis(cursor + 1)) {
      cursor += 4;
      continue;
    }
    break;
  }
  length = static_cast<size_t>(cursor - start);
  return length > 0;
}

// Match one rendered fragment against a persisted highlight token. Reflow may
// insert a hyphen or split punctuation into adjacent display fragments; NBSP
// is treated as equivalent to the ordinary space stored in the snippet.
inline TokenFragmentResult matchTokenFragment(const char* word, const bool endsWithInsertedHyphen, const char* token,
                                              const size_t tokenLength, const size_t tokenOffset) {
  if (!word || !token || tokenLength == 0 || tokenOffset >= tokenLength) return {};

  size_t wordLength = std::strlen(word);
  if (endsWithInsertedHyphen) {
    if (wordLength == 0 || word[wordLength - 1] != '-') return {};
    --wordLength;
  }

  size_t wordOffset = 0;
  size_t matchedTokenBytes = 0;
  while (wordOffset < wordLength && tokenOffset + matchedTokenBytes < tokenLength) {
    if (isNonBreakingSpace(word, wordLength, wordOffset) && token[tokenOffset + matchedTokenBytes] == ' ') {
      wordOffset += 2;
      ++matchedTokenBytes;
      continue;
    }
    if (word[wordOffset] != token[tokenOffset + matchedTokenBytes]) return {};
    ++wordOffset;
    ++matchedTokenBytes;
  }
  if (wordOffset != wordLength) return {};

  const bool completesToken = tokenOffset + matchedTokenBytes == tokenLength;
  if (completesToken && endsWithInsertedHyphen) return {};
  return {completesToken ? TokenFragmentMatch::COMPLETES_TOKEN : TokenFragmentMatch::CONTINUES_TOKEN,
          matchedTokenBytes};
}

struct Word {
  const char* text = nullptr;
  bool insertedHyphen = false;
};

struct Match {
  size_t start = 0;
  size_t end = 0;  // Inclusive rendered-word indices; valid only when found.
  bool found = false;
  bool ambiguous = false;
};

namespace detail {

inline bool isSpace(const uint32_t cp) {
  return cp == ' ' || (cp >= '\t' && cp <= '\r') || cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 ||
         cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

inline bool isCjk(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3130 && cp <= 0x318F) ||
         (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xA960 && cp <= 0xA97F) || (cp >= 0xAC00 && cp <= 0xD7FF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x323AF);
}

inline bool isPunctuation(const uint32_t cp) {
  return (cp >= '!' && cp <= '/') || (cp >= ':' && cp <= '@') || (cp >= '[' && cp <= '`') || (cp >= '{' && cp <= '~') ||
         (cp >= 0x2010 && cp <= 0x2027) || (cp >= 0x2030 && cp <= 0x205E) || (cp >= 0x3001 && cp <= 0x303F) ||
         (cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) ||
         (cp >= 0xFF5B && cp <= 0xFF65);
}

inline bool optionalSeparator(const uint32_t before, const uint32_t after) {
  // Old CJK layout exposed separate syllables and punctuation to the selection
  // UI, which inserted spaces between them. Do not erase Latin word boundaries.
  return isCjk(before) || isCjk(after) || isPunctuation(before) || isPunctuation(after);
}

inline bool readCodepoint(const char*& cursor, const char* end, uint32_t& cp) {
  if (cursor == end) return false;
  const auto first = static_cast<uint8_t>(*cursor++);
  if (first < 0x80) {
    cp = first;
    return true;
  }
  const unsigned extra = first >= 0xC2 && first <= 0xDF   ? 1
                         : first >= 0xE0 && first <= 0xEF ? 2
                         : first >= 0xF0 && first <= 0xF4 ? 3
                                                          : 0;
  if (extra == 0 || static_cast<size_t>(end - cursor) < extra) return false;
  cp = first & ((1U << (6 - extra)) - 1);
  for (unsigned index = 0; index < extra; ++index) {
    const auto next = static_cast<uint8_t>(*cursor++);
    if ((next & 0xC0) != 0x80) return false;
    cp = (cp << 6) | (next & 0x3F);
  }
  return cp >= (extra == 1   ? 0x80U
                : extra == 2 ? 0x800U
                             : 0x10000U) &&
         cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

struct Character {
  uint32_t codepoint = 0;
  bool separated = false;
  bool insertedHyphen = false;
};

// Streams either a snippet or rendered words without a normalized-text buffer.
template <typename GetWord>
class Cursor {
 public:
  Cursor(const size_t count, GetWord& getWord, const size_t start) : count(count), getWord(getWord), index(start) {
    loadWord();
  }

  bool next(Character& character) {
    bool separated = false;
    while (valid && index < count) {
      if (cursor == end) {
        separated = separated || !insertedHyphen;
        ++index;
        loadWord();
        continue;
      }
      uint32_t cp = 0;
      if (!readCodepoint(cursor, end, cp)) {
        valid = false;
        return false;
      }
      if (isSpace(cp)) {
        separated = true;
        continue;
      }
      character = {cp, separated, insertedHyphen && cursor == end && cp == '-'};
      return true;
    }
    return false;
  }

  bool atWordEnd() const {
    if (!valid || index >= count || insertedHyphen) return false;
    const char* rest = cursor;
    while (rest != end) {
      uint32_t cp = 0;
      if (!readCodepoint(rest, end, cp) || !isSpace(cp)) return false;
    }
    return true;
  }

  bool isValid() const { return valid; }
  size_t wordIndex() const { return index; }

 private:
  void loadWord() {
    if (index >= count) return;
    const Word word = getWord(index);
    cursor = word.text;
    if (!cursor) {
      valid = false;
      return;
    }
    end = cursor + std::strlen(cursor);
    insertedHyphen = word.insertedHyphen;
    if (insertedHyphen) {
      if (cursor == end || end[-1] != '-') {
        valid = false;
        return;
      }
      if (end - cursor == 1) valid = false;
    }
  }

  const size_t count;
  GetWord& getWord;
  size_t index;
  const char* cursor = nullptr;
  const char* end = nullptr;
  bool insertedHyphen = false;
  bool valid = true;
};

}  // namespace detail

// Require a complete phrase and whole rendered-word endpoints. In particular,
// never expand a legacy selection that now ends inside a larger Korean word.
// startLimit lets callers search adjacent pages together while counting only
// matches beginning on the first page. The getter's text must remain valid for
// the duration of this call. No text copies or heap allocations are made.
template <typename GetWord>
Match findUnique(const char* snippet, const size_t wordCount, GetWord getWord,
                 const size_t startLimit = static_cast<size_t>(-1)) {
  Match result;
  if (!snippet || !*snippet) return result;
  auto snippetWord = [snippet](size_t) { return Word{snippet, false}; };
  const detail::Cursor<decltype(snippetWord)> savedStart(1, snippetWord, 0);
  for (size_t candidate = 0; candidate < wordCount && candidate < startLimit; ++candidate) {
    if (candidate > 0 && getWord(candidate - 1).insertedHyphen) continue;
    detail::Cursor<GetWord> rendered(wordCount, getWord, candidate);
    auto saved = savedStart;
    detail::Character expected;
    detail::Character actual;
    uint32_t previous = 0;
    bool matched = false;
    while (saved.next(expected)) {
      bool hasActual = rendered.next(actual);
      // A current layout hyphen may also appear literally in an older snippet.
      // Accept that exact spelling, or skip only the flagged rendered hyphen.
      // Never remove a hyphen from saved text without its original provenance.
      while (hasActual && actual.insertedHyphen && actual.codepoint != expected.codepoint) {
        hasActual = rendered.next(actual);
      }
      if (!hasActual || actual.codepoint != expected.codepoint ||
          (previous == 0 && rendered.wordIndex() != candidate) ||
          (previous != 0 && actual.separated != expected.separated &&
           !detail::optionalSeparator(previous, actual.codepoint))) {
        matched = false;
        break;
      }
      previous = actual.codepoint;
      matched = true;
    }
    if (!matched || !saved.isValid() || !rendered.atWordEnd()) continue;
    if (result.found) return {0, 0, false, true};
    result = {candidate, rendered.wordIndex(), true, false};
  }
  return result;
}

}  // namespace HighlightTextMatcher
