#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "ChapterXPathIndexerInternal.h"

namespace ChapterXPathIndexerInternal {

// Shared parser state used by both forward and reverse mappers.
// It centralizes DOM-stack bookkeeping and XPath reconstruction so each mapper
// only implements its own match/emit logic.

struct StackNode {
  std::string tag;
  int index = 1;
  // Reserved for future text-node heuristics; intentionally unused for now.
  bool hasText = false;
};

struct StackState {
  int skipDepth = -1;
  size_t totalTextBytes = 0;
  std::vector<StackNode> stack;
  // Sibling-name → count map per parent depth. Index `d` holds the counts for
  // children that live at depth `d` in the DOM (i.e. queried just before
  // pushing a new node). Entries are cleared lazily on push rather than popped
  // and reallocated, so the per-element heap churn stays bounded.
  std::vector<std::unordered_map<std::string, int>> siblingCounters;

  StackState() {
    // Pre-size for typical EPUB chapter nesting (well below 32 levels). Avoids
    // per-element vector growth that would otherwise interleave with map node
    // allocations.
    stack.reserve(32);
    siblingCounters.resize(32);
  }

  void pushElement(const XML_Char* rawName) {
    const size_t depth = stack.size();
    if (siblingCounters.size() <= depth) {
      siblingCounters.resize(depth + 1);
    }
    // Lowercase the tag in place into the StackNode's own storage — the prior
    // implementation called toLowerStr() which returned a fresh std::string
    // per element, a major fragmentation source. Lookup into the parent's
    // sibling counter map then uses the stable in-place string with no extra
    // allocation.
    StackNode& node = stack.emplace_back();
    const char* localName = rawName ? rawName : "";
    if (const char* colon = std::strrchr(localName, ':')) {
      localName = colon + 1;
    }
    node.tag.assign(localName);
    for (char& c : node.tag) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const int sibIdx = ++siblingCounters[depth][node.tag];
    node.index = sibIdx;
    if (skipDepth < 0 && isSkippableTag(node.tag)) {
      skipDepth = static_cast<int>(stack.size()) - 1;
    }
  }

  void popElement() {
    if (stack.empty()) {
      return;
    }
    if (skipDepth == static_cast<int>(stack.size()) - 1) {
      skipDepth = -1;
    }
    // Clear the just-departed element's child-counter slot in place rather
    // than freeing the map: the next sibling at this depth needs an empty map
    // either way, and reusing the existing buckets avoids per-pop allocator
    // churn. We don't shrink siblingCounters for the same reason.
    const size_t childDepth = stack.size();
    if (childDepth < siblingCounters.size()) {
      siblingCounters[childDepth].clear();
    }
    stack.pop_back();
  }

  void onCharData(const XML_Char*, int) {}

  int bodyIdx() const {
    for (int i = static_cast<int>(stack.size()) - 1; i >= 0; i--) {
      if (stack[i].tag == "body") {
        return i;
      }
    }
    return -1;
  }

  bool insideBody() const { return bodyIdx() >= 0; }

  // Out-parameter form: appends the path into `out` without freeing it first
  // so the caller controls when to reuse vs reset capacity. Use this in hot
  // paths to amortise the underlying allocation.
  void buildCurrentXPath(const int spineIndex, std::string& out) const {
    out.clear();
    out.append("/body/DocFragment[");
    appendInt(out, spineIndex + 1);
    out.append("]/body");
    const int bi = bodyIdx();
    if (bi < 0) {
      return;
    }
    for (size_t i = static_cast<size_t>(bi + 1); i < stack.size(); i++) {
      out.push_back('/');
      out.append(stack[i].tag);
      out.push_back('[');
      appendInt(out, stack[i].index);
      out.push_back(']');
    }
  }

  std::string currentXPath(const int spineIndex) const {
    std::string out;
    buildCurrentXPath(spineIndex, out);
    return out;
  }

  bool shouldSkipText(const int len) const { return skipDepth >= 0 || len <= 0 || !insideBody(); }

 private:
  // Appends a non-negative int as decimal digits without allocating a temp
  // std::string (std::to_string would allocate per call).
  static void appendInt(std::string& out, int value) {
    if (value < 0) {
      out.push_back('-');
      value = -value;
    }
    char buf[12];
    int len = 0;
    if (value == 0) {
      buf[len++] = '0';
    } else {
      while (value > 0) {
        buf[len++] = static_cast<char>('0' + (value % 10));
        value /= 10;
      }
    }
    while (len-- > 0) {
      out.push_back(buf[len]);
    }
  }
};

template <typename StateT>
void XMLCALL parserStartCb(void* ud, const XML_Char* name, const XML_Char**) {
  static_cast<StateT*>(ud)->onStartElement(name);
}

template <typename StateT>
void XMLCALL parserEndCb(void* ud, const XML_Char*) {
  static_cast<StateT*>(ud)->onEndElement();
}

template <typename StateT>
void XMLCALL parserCharCb(void* ud, const XML_Char* text, const int len) {
  static_cast<StateT*>(ud)->onCharData(text, len);
}

template <typename StateT>
void XMLCALL parserDefaultCb(void* ud, const XML_Char* text, const int len) {
  if (isEntityRef(text, len)) {
    static_cast<StateT*>(ud)->onCharData(text, len);
  }
}

}  // namespace ChapterXPathIndexerInternal
