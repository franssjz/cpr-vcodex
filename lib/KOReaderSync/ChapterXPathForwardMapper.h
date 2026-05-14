#pragma once

#include <Epub.h>

#include <memory>
#include <string>

namespace ChapterXPathIndexerInternal {

std::string findXPathForProgressInternal(const std::shared_ptr<Epub>& epub, int spineIndex, float intraSpineProgress);

// Find the full-ancestry XPath for the paragraphIndex-th direct-body-child <p> element.
// paragraphIndex is 1-based, matching the section paragraph LUT and KOReader XPath convention.
// seekHint is an optional XHTML byte offset to start scanning from (0 = scan from beginning).
// startParagraphCount is the number of body-child <p> elements already seen before seekHint
// (i.e. the paragraphIndex of the LUT entry at the seek page, minus 1). Ignored when seekHint=0.
// Returns empty string on failure; caller should fall back to findXPathForProgressInternal.
std::string findXPathForParagraphInternal(const std::shared_ptr<Epub>& epub, int spineIndex, uint16_t paragraphIndex,
                                          uint32_t seekHint = 0, uint16_t startParagraphCount = 0);

}  // namespace ChapterXPathIndexerInternal
