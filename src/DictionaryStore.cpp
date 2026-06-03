#include "DictionaryStore.h"

#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <StarDict.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

DictionaryStore DictionaryStore::instance;

constexpr char DictionaryStore::CONFIG_PATH[];
constexpr char DictionaryStore::DICT_ROOT[];

DictionaryStore& DictionaryStore::getInstance() { return instance; }

// ---------------------------------------------------------------------------
// scan()
// ---------------------------------------------------------------------------

void DictionaryStore::scan() {
  // Keep existing entries so we can preserve enabled/order for known dicts.
  std::vector<DictEntry> previous = std::move(entries);
  entries.clear();

  // Helper: find a previous entry by basePath.
  auto findPrev = [&](const std::string& path) -> const DictEntry* {
    for (const auto& e : previous) {
      if (e.basePath == path) {
        return &e;
      }
    }
    return nullptr;
  };

  // Collect .ifo file paths from DICT_ROOT (non-recursive, one level deep).
  // We open the directory and iterate its children; both flat layout
  // (/dictionaries/dict.ifo) and one-level sub-folder layout
  // (/dictionaries/mydict/dict.ifo) are supported.
  std::vector<std::string> ifoFiles;

  auto scanDir = [&](const std::string& dirPath) {
    HalFile dir = Storage.open(dirPath.c_str());
    if (!dir.isOpen() || !dir.isDirectory()) {
      return;
    }
    dir.rewindDirectory();
    while (true) {
      HalFile child = dir.openNextFile();
      if (!child.isOpen()) {
        break;
      }
      char nameBuf[64];
      child.getName(nameBuf, sizeof(nameBuf));
      const std::string childName(nameBuf);

      if (child.isDirectory()) {
        // One level deep: scan inside sub-folder
        const std::string subPath = dirPath + "/" + childName;
        HalFile sub = Storage.open(subPath.c_str());
        if (sub.isOpen() && sub.isDirectory()) {
          sub.rewindDirectory();
          while (true) {
            HalFile subChild = sub.openNextFile();
            if (!subChild.isOpen()) break;
            char subName[64];
            subChild.getName(subName, sizeof(subName));
            const std::string subFile(subName);
            if (FsHelpers::checkFileExtension(subFile, ".ifo")) {
              ifoFiles.push_back(subPath + "/" + subFile);
            }
          }
        }
      } else if (FsHelpers::checkFileExtension(childName, ".ifo")) {
        ifoFiles.push_back(dirPath + "/" + childName);
      }
    }
  };

  scanDir(DICT_ROOT);

  // Sort for deterministic order
  std::sort(ifoFiles.begin(), ifoFiles.end());

  // Open each .ifo to get the bookname; preserve previous state.
  for (const auto& ifoPath : ifoFiles) {
    StarDict sd;
    if (!sd.open(ifoPath)) {
      continue;
    }

    DictEntry entry;
    entry.basePath = ifoPath;
    entry.name = sd.getName();
    entry.lang = sd.getLanguage();

    const DictEntry* prev = findPrev(ifoPath);
    entry.enabled = prev ? prev->enabled : true;

    entries.push_back(std::move(entry));
    sd.close();
  }

  // Re-apply previous order for dicts that survived the scan.
  // New dicts (not in previous) stay at the end in alphabetical order.
  // Strategy: build an order vector based on the position of each entry in
  // `previous`, then sort; new dicts keep their current position.
  std::stable_sort(entries.begin(), entries.end(), [&](const DictEntry& a, const DictEntry& b) {
    int posA = static_cast<int>(previous.size());
    int posB = static_cast<int>(previous.size());
    for (int i = 0; i < static_cast<int>(previous.size()); ++i) {
      if (previous[i].basePath == a.basePath) posA = i;
      if (previous[i].basePath == b.basePath) posB = i;
    }
    return posA < posB;
  });

  LOG_DBG("DICT", "scan: found %u dictionaries", static_cast<unsigned>(entries.size()));
}

// ---------------------------------------------------------------------------
// saveConfig / loadConfig
// ---------------------------------------------------------------------------

bool DictionaryStore::saveConfig() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  JsonArray arr = doc["dicts"].to<JsonArray>();
  for (const auto& e : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = e.basePath;
    obj["enabled"] = e.enabled;
  }

  const std::string tempPath = std::string(CONFIG_PATH) + ".tmp";
  Storage.remove(tempPath.c_str());

  HalFile f;
  if (!Storage.openFileForWrite("DICT", tempPath, f)) {
    LOG_ERR("DICT", "saveConfig: cannot open temp file");
    return false;
  }

  serializeJson(doc, f);
  f.flush();
  f.close();

  Storage.remove(CONFIG_PATH);
  if (!Storage.rename(tempPath.c_str(), CONFIG_PATH)) {
    LOG_ERR("DICT", "saveConfig: rename failed");
    return false;
  }
  return true;
}

bool DictionaryStore::loadConfig() {
  if (!Storage.exists(CONFIG_PATH)) {
    return false;
  }

  const String json = Storage.readFile(CONFIG_PATH);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    LOG_ERR("DICT", "loadConfig: JSON parse error");
    return false;
  }

  JsonArray arr = doc["dicts"].as<JsonArray>();
  if (!arr) {
    return false;
  }

  // Build a new ordered list based on the saved config; entries present in
  // `entries` (from scan()) but not in config are appended at the end.
  std::vector<DictEntry> ordered;
  ordered.reserve(entries.size());

  for (JsonObject obj : arr) {
    const char* path = obj["path"] | "";
    const bool enabled = obj["enabled"] | true;

    for (auto& e : entries) {
      if (e.basePath == std::string(path)) {
        e.enabled = enabled;
        ordered.push_back(e);
        break;
      }
    }
  }

  // Append any entries from `entries` that were not in the config.
  for (const auto& e : entries) {
    bool found = false;
    for (const auto& o : ordered) {
      if (o.basePath == e.basePath) {
        found = true;
        break;
      }
    }
    if (!found) {
      ordered.push_back(e);
    }
  }

  entries = std::move(ordered);
  return true;
}

// ---------------------------------------------------------------------------
// lookup
// ---------------------------------------------------------------------------

// In-place HTML tag removal for StarDict definitions.
// Strips <tags>, converts common &entities;, inserts a space at block elements.
static void stripHtml(char* buf) {
  if (!buf || buf[0] == '\0') return;
  if (!strchr(buf, '<') && !strchr(buf, '&')) return;

  char* rd = buf;
  char* wr = buf;
  bool inTag = false;

  while (*rd) {
    if (*rd == '<') {
      inTag = true;
      // Insert a space at block-level tags so words don't merge.
      const char* p = rd + 1;
      if (*p == '/') p++;
      const bool isBr  = tolower((unsigned char)p[0]) == 'b' && tolower((unsigned char)p[1]) == 'r';
      const bool isP   = tolower((unsigned char)p[0]) == 'p' && (p[1] == '>' || p[1] == ' ');
      const bool isDiv = tolower((unsigned char)p[0]) == 'd' && tolower((unsigned char)p[1]) == 'i';
      const bool isLi  = tolower((unsigned char)p[0]) == 'l' && tolower((unsigned char)p[1]) == 'i';
      if ((isBr || isP || isDiv || isLi) && wr > buf && wr[-1] != ' ') *wr++ = ' ';
      rd++;
    } else if (inTag) {
      if (*rd == '>') inTag = false;
      rd++;
    } else if (*rd == '&') {
      if      (strncmp(rd, "&amp;",  5) == 0) { *wr++ = '&';  rd += 5; }
      else if (strncmp(rd, "&lt;",   4) == 0) { *wr++ = '<';  rd += 4; }
      else if (strncmp(rd, "&gt;",   4) == 0) { *wr++ = '>';  rd += 4; }
      else if (strncmp(rd, "&quot;", 6) == 0) { *wr++ = '"';  rd += 6; }
      else if (strncmp(rd, "&nbsp;", 6) == 0) { *wr++ = ' ';  rd += 6; }
      else if (strncmp(rd, "&apos;", 6) == 0) { *wr++ = '\''; rd += 6; }
      else                                     { *wr++ = *rd++; }
    } else {
      *wr++ = *rd++;
    }
  }
  *wr = '\0';

  // Collapse multiple spaces and trim leading/trailing whitespace.
  rd = buf;
  while (*rd == ' ') rd++;
  wr = buf;
  bool lastSpace = false;
  while (*rd) {
    if (*rd == ' ') {
      if (!lastSpace) *wr++ = ' ';
      lastSpace = true;
    } else {
      *wr++ = *rd;
      lastSpace = false;
    }
    rd++;
  }
  while (wr > buf && wr[-1] == ' ') wr--;
  *wr = '\0';
}

// Simple English suffix-stripping fallback.
// Adds candidate stems to `out` (exact word is NOT included — caller handles it).
// Only called when exact lookup fails, so allocations are acceptable.
static void addFallbackForms(const char* word, std::vector<std::string>& out) {
  const size_t len = strlen(word);
  if (len < 3) return;

  std::string lc(word);
  for (auto& c : lc) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (lc != word) out.push_back(lc);

  const size_t n = lc.size();

  auto push = [&](std::string s) { if (s.size() >= 2) out.push_back(std::move(s)); };

  static const char* const consonants = "bcdfghjklmnpqrstvwxyz";

  // -ing: spieling→spiel, making→make, running→run
  if (n > 4 && lc.compare(n - 3, 3, "ing") == 0) {
    const std::string stem = lc.substr(0, n - 3);
    push(stem);
    push(stem + "e");
    if (stem.size() >= 2 && stem.back() == stem[stem.size() - 2] &&
        strchr(consonants, stem.back())) {
      push(stem.substr(0, stem.size() - 1));
    }
  }

  // -ed: played→play, loved→love, stopped→stop
  if (n > 3 && lc.compare(n - 2, 2, "ed") == 0) {
    const std::string stem = lc.substr(0, n - 2);
    push(stem);
    push(stem + "e");
    if (stem.size() >= 2 && stem.back() == stem[stem.size() - 2] &&
        strchr(consonants, stem.back())) {
      push(stem.substr(0, stem.size() - 1));
    }
  }

  // -s / -es / -ies: books→book, foxes→fox, flies→fly
  if (n > 2 && lc.back() == 's') {
    push(lc.substr(0, n - 1));
    if (n > 3 && lc.compare(n - 2, 2, "es") == 0) push(lc.substr(0, n - 2));
    if (n > 4 && lc.compare(n - 3, 3, "ies") == 0) push(lc.substr(0, n - 3) + "y");
  }

  // -er / -est: nicer→nice, fastest→fast
  if (n > 3 && lc.compare(n - 2, 2, "er") == 0) {
    push(lc.substr(0, n - 2));
    push(lc.substr(0, n - 2) + "e");
  }
  if (n > 4 && lc.compare(n - 3, 3, "est") == 0) {
    push(lc.substr(0, n - 3));
    push(lc.substr(0, n - 3) + "e");
  }

  // -ly: quickly→quick
  if (n > 3 && lc.compare(n - 2, 2, "ly") == 0) push(lc.substr(0, n - 2));

  // -ness: kindness→kind
  if (n > 5 && lc.compare(n - 4, 4, "ness") == 0) push(lc.substr(0, n - 4));
}
// Matching rules (case-insensitive, ISO 639-1 prefix):
//  - Either lang unknown (empty / <2 chars) → include (conservative)
//  - dictLang looks like a natural name (no '-', length > 3) → include
//    (e.g. "Turkish" — cannot reliably map to ISO code)
//  - Otherwise compare only the FIRST token (source language) of dictLang
//    against the first two chars of bookLang.
//    e.g. "en-tr" → source="en", used for English books only.
//         "tr-en" → source="tr", used for Turkish books.
//         "tr"   → source="tr", used for Turkish books.
static bool dictLangMatchesBook(const std::string& dictLang, const char* bookLang) {
  if (bookLang == nullptr || bookLang[0] == '\0' || bookLang[1] == '\0') {
    return true;  // book lang unknown → no filter
  }
  if (dictLang.size() < 2) {
    return true;  // dict lang unknown → always try
  }

  const char bl0 = static_cast<char>(tolower(static_cast<unsigned char>(bookLang[0])));
  const char bl1 = static_cast<char>(tolower(static_cast<unsigned char>(bookLang[1])));

  // Natural-language name (e.g. "Turkish") — cannot parse → include.
  const bool hasDelimiter = (dictLang.find('-') != std::string::npos);
  if (!hasDelimiter && dictLang.size() > 3) {
    return true;
  }

  // Only the first token (source language) must match.
  const size_t end = dictLang.find('-');
  const size_t tokenLen = (end == std::string::npos) ? dictLang.size() : end;
  if (tokenLen < 2) {
    return true;  // malformed → include
  }
  const char t0 = static_cast<char>(tolower(static_cast<unsigned char>(dictLang[0])));
  const char t1 = static_cast<char>(tolower(static_cast<unsigned char>(dictLang[1])));
  return (t0 == bl0 && t1 == bl1);
}

bool DictionaryStore::lookup(const char* word, char* definition, size_t maxLen,
                             const char* bookLang) const {
  if (!word || !definition || maxLen == 0) {
    return false;
  }
  definition[0] = '\0';

  // Build candidate list: exact word first, then suffix-stripped fallbacks.
  std::vector<std::string> candidates;
  candidates.reserve(12);
  candidates.push_back(word);
  addFallbackForms(word, candidates);

  for (const auto& entry : entries) {
    if (!entry.enabled) {
      continue;
    }

    // Skip dictionaries whose language is incompatible with the book language.
    if (!dictLangMatchesBook(entry.lang, bookLang)) {
      LOG_DBG("DICT", "lookup: skipping '%s' (lang '%s' != book '%s')",
              entry.name.c_str(), entry.lang.c_str(), bookLang ? bookLang : "");
      continue;
    }

    StarDict sd;
    if (!sd.open(entry.basePath)) {
      continue;
    }

    for (const auto& candidate : candidates) {
      if (sd.lookup(candidate.c_str(), definition, maxLen)) {
        stripHtml(definition);
        LOG_DBG("DICT", "lookup '%s' (via '%s') found in '%s'",
                word, candidate.c_str(), entry.name.c_str());
        return true;
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// moveUp / moveDown / setEnabled
// ---------------------------------------------------------------------------

void DictionaryStore::moveUp(size_t index) {
  if (index == 0 || index >= entries.size()) {
    return;
  }
  std::swap(entries[index], entries[index - 1]);
}

void DictionaryStore::moveDown(size_t index) {
  if (index + 1 >= entries.size()) {
    return;
  }
  std::swap(entries[index], entries[index + 1]);
}

void DictionaryStore::setEnabled(size_t index, bool enabled) {
  if (index < entries.size()) {
    entries[index].enabled = enabled;
  }
}
