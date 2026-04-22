#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

// Bionic Reading helpers — no heap, no std::string, stack-only slicing.

// Faithful port of metaguiding.py:78 — midpoint = 1 if n in (1,3) else ceil(n/2)
static constexpr int bionicMidpoint(int n) {
  return (n == 1 || n == 3) ? 1 : (n + 1) / 2;
}

// Count UTF-8 codepoints in [begin, end) by skipping continuation bytes.
static int utf8CodepointCount(const char* begin, const char* end) {
  int n = 0;
  for (const char* p = begin; p < end; ++p) {
    if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) ++n;
  }
  return n;
}

// Mirrors Python's \w under re.UNICODE: ASCII alnum/underscore + all non-ASCII bytes (UTF-8).
static inline bool isWordByte(uint8_t b) {
  if (b >= 0x80) return true;
  return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b == '_');
}

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y,
                       const bool bionicReading) const {
  // Validate iterator bounds before rendering
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    LOG_ERR("TXB", "Render skipped: size mismatch (words=%u, xpos=%u, styles=%u)\n", (uint32_t)words.size(),
            (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size());
    return;
  }

  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = wordXpos[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    const std::string& w = words[i];

    // Fast paths: bionic off, already-bold, or word too long for stack slice buffer.
    const bool alreadyBold = (currentStyle & EpdFontFamily::BOLD) != 0;
    if (!bionicReading || alreadyBold || w.size() >= 128) {
      renderer.drawText(fontId, wordX, y, w.c_str(), true, currentStyle);
    } else {
      // Stack slice buffer (<128 bytes, well within CLAUDE.md <256 byte rule).
      char buf[128];
      const EpdFontFamily::Style boldStyle =
          static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      int cursorX = wordX;
      size_t i0 = 0;

      while (i0 < w.size()) {
        // Non-word run: draw in original style, advance cursor.
        size_t j = i0;
        while (j < w.size() && !isWordByte(static_cast<uint8_t>(w[j]))) ++j;
        if (j > i0) {
          const size_t n = j - i0;
          memcpy(buf, w.data() + i0, n);
          buf[n] = '\0';
          renderer.drawText(fontId, cursorX, y, buf, true, currentStyle);
          cursorX += renderer.getTextAdvanceX(fontId, buf, currentStyle);
          i0 = j;
          if (i0 >= w.size()) break;
        }

        // Word run: bold the first M codepoints, regular for the rest.
        size_t k = i0;
        while (k < w.size() && isWordByte(static_cast<uint8_t>(w[k]))) ++k;

        const int ncp = utf8CodepointCount(w.data() + i0, w.data() + k);
        const int mcp = bionicMidpoint(ncp);

        // Find byte boundary after the M-th codepoint.
        size_t splitByte = i0;
        {
          size_t p = i0;
          int seen = 0;
          while (p < k && seen < mcp) {
            ++p;
            while (p < k && (static_cast<uint8_t>(w[p]) & 0xC0) == 0x80) ++p;
            ++seen;
          }
          splitByte = p;
        }

        // Bold prefix.
        {
          const size_t n = splitByte - i0;
          memcpy(buf, w.data() + i0, n);
          buf[n] = '\0';
          renderer.drawText(fontId, cursorX, y, buf, true, boldStyle);
          cursorX += renderer.getTextAdvanceX(fontId, buf, boldStyle);
        }

        // Regular suffix (if any).
        if (splitByte < k) {
          const size_t n = k - splitByte;
          memcpy(buf, w.data() + splitByte, n);
          buf[n] = '\0';
          renderer.drawText(fontId, cursorX, y, buf, true, currentStyle);
          cursorX += renderer.getTextAdvanceX(fontId, buf, currentStyle);
        }

        i0 = k;
      }
    }

    if ((currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      // y is the top of the text line; add ascender to reach baseline, then offset 2px below
      const int underlineY = y + renderer.getFontAscenderSize(fontId) + 2;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      // if word starts with em-space ("\xe2\x80\x83"), account for the additional indent before drawing the line
      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, true);
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    LOG_ERR("TXB", "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u)\n", words.size(),
            wordXpos.size(), wordStyles.size());
    return false;
  }

  // Word data
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

  // Word count
  serialization::readPod(file, wc);

  // Sanity check: prevent allocation of unreasonably large vectors (max 10000 words per block)
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  for (auto& w : words) serialization::readString(file, w);
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);

  return std::unique_ptr<TextBlock>(
      new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), blockStyle));
}
