#include "ChapterXPathIndexerInternal.h"

#include <Epub/htmlEntities.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace ChapterXPathIndexerInternal {

std::string toLowerStr(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool isSkippableTag(const std::string& tag) { return tag == "head" || tag == "script" || tag == "style"; }

bool isWhitespaceOnly(const XML_Char* text, const int len) {
  for (int i = 0; i < len; i++) {
    if (!std::isspace(static_cast<unsigned char>(text[i]))) {
      return false;
    }
  }
  return true;
}

static size_t countVisibleBytesInUtf8String(const char* str) {
  size_t count = 0;
  for (const unsigned char* ptr = reinterpret_cast<const unsigned char*>(str); *ptr != 0; ++ptr) {
    if (!std::isspace(*ptr)) {
      count++;
    }
  }
  return count;
}

size_t countVisibleBytes(const XML_Char* text, const int len) {
  if (isEntityRef(text, len)) {
    const char* resolved = lookupHtmlEntity(text, static_cast<size_t>(len));
    if (resolved) {
      return countVisibleBytesInUtf8String(resolved);
    }
  }

  size_t count = 0;
  for (int i = 0; i < len; i++) {
    if (!std::isspace(static_cast<unsigned char>(text[i]))) {
      count++;
    }
  }
  return count;
}

size_t countUtf8Codepoints(const XML_Char* text, const int len) {
  if (isEntityRef(text, len)) {
    const char* resolved = lookupHtmlEntity(text, static_cast<size_t>(len));
    if (resolved) {
      size_t count = 0;
      const unsigned char* ptr = reinterpret_cast<const unsigned char*>(resolved);
      while (*ptr != 0) {
        utf8NextCodepoint(&ptr);
        count++;
      }
      return count;
    }
  }

  size_t count = 0;
  for (int i = 0; i < len; i++) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
      count++;
    }
  }
  return count;
}

size_t codepointAtVisibleByte(const XML_Char* text, const int len, const size_t targetVisibleByte) {
  if (isEntityRef(text, len)) {
    const char* resolved = lookupHtmlEntity(text, static_cast<size_t>(len));
    if (resolved) {
      size_t codepoints = 0;
      size_t visibleBytes = 0;
      const unsigned char* ptr = reinterpret_cast<const unsigned char*>(resolved);
      while (*ptr != 0) {
        const unsigned char* cpStart = ptr;
        utf8NextCodepoint(&ptr);
        codepoints++;
        for (const unsigned char* it = cpStart; it < ptr; ++it) {
          if (!std::isspace(*it)) {
            if (visibleBytes == targetVisibleByte) {
              return codepoints - 1;
            }
            visibleBytes++;
          }
        }
      }
      return codepoints;
    }
  }

  size_t codepoints = 0;
  size_t visibleBytes = 0;
  for (int i = 0; i < len; i++) {
    const unsigned char uc = static_cast<unsigned char>(text[i]);
    const bool isLeadByte = (uc & 0xC0) != 0x80;
    if (isLeadByte) {
      codepoints++;
    }
    if (!std::isspace(uc)) {
      if (visibleBytes == targetVisibleByte) {
        return codepoints - 1;
      }
      visibleBytes++;
    }
  }
  return codepoints;
}

size_t visibleBytesBeforeCodepoint(const XML_Char* text, const int len, const size_t targetCodepointOffset) {
  if (isEntityRef(text, len)) {
    const char* resolved = lookupHtmlEntity(text, static_cast<size_t>(len));
    if (resolved) {
      size_t visibleBytes = 0;
      size_t codepointIndex = 0;
      const unsigned char* ptr = reinterpret_cast<const unsigned char*>(resolved);
      while (*ptr != 0 && codepointIndex < targetCodepointOffset) {
        const unsigned char* cpStart = ptr;
        utf8NextCodepoint(&ptr);
        for (const unsigned char* it = cpStart; it < ptr; ++it) {
          if (!std::isspace(*it)) {
            visibleBytes++;
          }
        }
        codepointIndex++;
      }
      return visibleBytes;
    }
  }

  size_t visibleBytes = 0;
  size_t codepointIndex = 0;

  int i = 0;
  while (i < len) {
    if (codepointIndex >= targetCodepointOffset) {
      break;
    }

    const int cpStart = i;
    i++;
    while (i < len && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
      i++;
    }

    for (int j = cpStart; j < i; j++) {
      if (!std::isspace(static_cast<unsigned char>(text[j]))) {
        visibleBytes++;
      }
    }

    codepointIndex++;
  }

  return visibleBytes;
}

// Thread-local-free scratch reused across normalizeXPath() invocations so the
// two-phase rewrite (lowercase/strip pass → bare-element-predicate pass) costs
// at most one growing std::string per process lifetime instead of two per call.
// Single-threaded on ESP32, so a function-local static is safe.
void normalizeXPath(const std::string& input, std::string& out) {
  out.clear();
  if (input.empty()) {
    return;
  }

  static std::string firstPass;
  firstPass.clear();
  firstPass.reserve(input.size());
  for (const char c : input) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isspace(uc)) {
      continue;
    }
    firstPass.push_back(static_cast<char>(std::tolower(uc)));
  }

  const std::string textTag = "/text()";
  const size_t textPos = firstPass.rfind(textTag);
  if (textPos != std::string::npos) {
    const size_t afterText = textPos + textTag.size();
    if (afterText == firstPass.size() || firstPass[afterText] == '.' || firstPass[afterText] == '[') {
      firstPass.erase(textPos);
    }
  }

  const size_t lastSlash = firstPass.rfind('/');
  if (lastSlash != std::string::npos) {
    const size_t dotPos = firstPass.find('.', lastSlash + 1);
    if (dotPos != std::string::npos && dotPos + 1 < firstPass.size()) {
      bool allDigits = true;
      for (size_t i = dotPos + 1; i < firstPass.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(firstPass[i]))) {
          allDigits = false;
          break;
        }
      }
      if (allDigits) {
        firstPass.erase(dotPos);
      }
    }
  }

  while (!firstPass.empty() && firstPass.back() == '/') {
    firstPass.pop_back();
  }

  // KOReader sometimes omits the [1] predicate for elements that are the sole
  // child of their type (e.g. /body/div/p[55] instead of /body/div[1]/p[55]).
  // In XPath, an unqualified name is equivalent to name[1] when there is only
  // one sibling of that type, but our parser always generates explicit indices.
  // Insert [1] for any bare element path segment so comparisons match.
  out.reserve(firstPass.size() + 16);
  size_t i = 0;
  while (i < firstPass.size()) {
    if (firstPass[i] == '/') {
      out.push_back('/');
      i++;
      // Copy element name (letters, digits, hyphens, underscores, dots)
      const size_t nameStart = i;
      while (i < firstPass.size() && firstPass[i] != '/' && firstPass[i] != '[') {
        i++;
      }
      out.append(firstPass, nameStart, i - nameStart);
      if (i < firstPass.size() && firstPass[i] == '[') {
        // Already has a predicate – copy it verbatim
        while (i < firstPass.size() && firstPass[i] != ']') {
          out.push_back(firstPass[i++]);
        }
        if (i < firstPass.size()) {
          out.push_back(firstPass[i++]);  // ']'
        }
      } else if (i - nameStart > 0) {
        // Bare element name – insert implicit [1]
        out.append("[1]");
      }
    } else {
      out.push_back(firstPass[i++]);
    }
  }
}

std::string normalizeXPath(const std::string& input) {
  std::string out;
  normalizeXPath(input, out);
  return out;
}

void removeIndices(const std::string& xpath, std::string& out) {
  out.clear();
  out.reserve(xpath.size());
  bool inBracket = false;
  for (const char c : xpath) {
    if (c == '[') {
      inBracket = true;
      continue;
    }
    if (c == ']') {
      inBracket = false;
      continue;
    }
    if (!inBracket) {
      out.push_back(c);
    }
  }
}

std::string removeIndices(const std::string& xpath) {
  std::string out;
  removeIndices(xpath, out);
  return out;
}

int pathDepth(const std::string& xpath) {
  int depth = 0;
  for (const char c : xpath) {
    if (c == '/') {
      depth++;
    }
  }
  return depth;
}

bool isAncestorPath(const std::string& prefix, const std::string& path) {
  return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/';
}

std::string decompressToTempFile(const std::shared_ptr<Epub>& epub, const int spineIndex) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto spineItem = epub->getSpineItem(spineIndex);
  if (spineItem.href.empty()) {
    return "";
  }

  const std::string tmpPath = epub->getCachePath() + "/.tmp_kox_" + std::to_string(spineIndex) + ".html";
  if (Storage.exists(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
  }

  FsFile tmpFile;
  if (!Storage.openFileForWrite("KOX", tmpPath, tmpFile)) {
    LOG_ERR("KOX", "Failed to create temp file for spine=%d", spineIndex);
    return "";
  }

  constexpr size_t kChunkSize = 1024;
  const bool ok = epub->readItemContentsToStream(spineItem.href, tmpFile, kChunkSize);
  tmpFile.close();

  if (!ok) {
    Storage.remove(tmpPath.c_str());
    LOG_ERR("KOX", "Failed to decompress spine=%d to temp file", spineIndex);
    return "";
  }

  return tmpPath;
}

namespace {
// Pump the open `file` through `parser` in fixed-size chunks. Returns true on clean EOF or
// XML_ERROR_ABORTED (caller used XML_StopParser to signal an early success). Returns false on
// XML_GetBuffer failure or any other parse error. The file is left open — caller closes it.
bool pumpExpatFromFile(XML_Parser parser, FsFile& file) {
  constexpr size_t kBufSize = 1024;
  int done;
  do {
    void* const buf = XML_GetBuffer(parser, kBufSize);
    if (!buf) {
      return false;
    }
    const size_t len = file.read(buf, kBufSize);
    done = file.available() == 0;
    if (XML_ParseBuffer(parser, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      return XML_GetErrorCode(parser) == XML_ERROR_ABORTED;
    }
  } while (!done);
  return true;
}
}  // namespace

bool runParse(XML_Parser parser, const std::string& path) {
  FsFile file;
  if (!Storage.openFileForRead("KOX", path, file)) {
    return false;
  }
  const bool ok = pumpExpatFromFile(parser, file);
  file.close();
  return ok;
}

// Starts Expat mid-document. Since the parser has no ancestor context (html/body stack is
// missing), unmatched closing tags may appear, and callbacks emitted before the first start
// tag can look structurally odd — an empty result is normal here. Callers
// (ChapterXPathForwardMapper.cpp) recognise the empty result and fall back to runParse from
// byte 0 with full document context.
bool runParseFromOffset(XML_Parser parser, const std::string& path, const uint32_t seekBytes) {
  if (seekBytes == 0) {
    return runParse(parser, path);
  }

  FsFile file;
  if (!Storage.openFileForRead("KOX", path, file)) {
    return false;
  }

  if (!file.seek(seekBytes)) {
    file.close();
    return runParse(parser, path);  // fall back to full scan if seek fails
  }

  const bool ok = pumpExpatFromFile(parser, file);
  file.close();
  return ok;
}

bool isEntityRef(const XML_Char* text, const int len) {
  if (len < 3 || text[0] != '&' || text[len - 1] != ';') {
    return false;
  }
  for (int i = 1; i < len - 1; ++i) {
    if (text[i] == '<' || text[i] == '>') {
      return false;
    }
  }
  return true;
}

namespace {

struct ByteCounter {
  int skipDepth = -1;
  int bodyStartDepth = -1;
  int depth = 0;
  size_t totalTextBytes = 0;
};

void XMLCALL bcStart(void* ud, const XML_Char* name, const XML_Char**) {
  auto* s = static_cast<ByteCounter*>(ud);
  const std::string tag = toLowerStr(name ? name : "");
  if (tag == "body" && s->bodyStartDepth < 0) {
    s->bodyStartDepth = s->depth;
  }
  if (s->skipDepth < 0 && isSkippableTag(tag)) {
    s->skipDepth = s->depth;
  }
  s->depth++;
}

void XMLCALL bcEnd(void* ud, const XML_Char*) {
  auto* s = static_cast<ByteCounter*>(ud);
  s->depth--;
  if (s->depth == s->skipDepth) {
    s->skipDepth = -1;
  }
  if (s->depth == s->bodyStartDepth) {
    s->bodyStartDepth = -1;
  }
}

void XMLCALL bcChar(void* ud, const XML_Char* text, const int len) {
  auto* s = static_cast<ByteCounter*>(ud);
  if (s->skipDepth >= 0 || s->bodyStartDepth < 0 || len <= 0 || isWhitespaceOnly(text, len)) {
    return;
  }
  s->totalTextBytes += countVisibleBytes(text, len);
}

void XMLCALL bcDefault(void* ud, const XML_Char* text, const int len) {
  if (isEntityRef(text, len)) {
    bcChar(ud, text, len);
  }
}

}  // namespace

size_t countTotalTextBytes(const std::string& tmpPath) {
  ByteCounter state;
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    return 0;
  }
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, bcStart, bcEnd);
  XML_SetCharacterDataHandler(parser, bcChar);
  XML_SetDefaultHandlerExpand(parser, bcDefault);
  const bool ok = runParse(parser, tmpPath);
  XML_ParserFree(parser);
  return ok ? state.totalTextBytes : 0;
}

}  // namespace ChapterXPathIndexerInternal
