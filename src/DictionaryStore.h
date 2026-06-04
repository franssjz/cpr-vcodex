#pragma once

#include <cstddef>
#include <string>
#include <vector>

// DictionaryStore manages StarDict dictionaries placed on the SD card under
// /dictionaries/   (each dictionary lives in its own sub-folder or directly).
//
// Usage flow:
//  1. Call scan() once after the SD card is mounted (e.g. at boot or on demand).
//  2. Call loadConfig() to restore the user's enabled-and-ordered list.
//  3. Call lookup() from the reader to look up a word.
//  4. Call saveConfig() whenever the user changes the enabled/order list.
//
// Dictionaries are tried in the order returned by getEntries().  The first
// hit is returned; remaining dictionaries are not searched.
//
// Config is persisted in /.crosspoint/dict_config.json.

struct DictEntry {
  std::string name;      // Human-readable (from .ifo bookname)
  std::string basePath;  // Absolute path of the .ifo file (identifies the dict)
  std::string lang;      // Language tag from .ifo (e.g. "tr", "en-tr", may be empty)
  bool enabled = true;
  mutable std::vector<uint32_t> idxCheckpoints;  // built by scan() for reliable lookup
};

class DictionaryStore {
 public:
  static DictionaryStore& getInstance();

  // Scan /dictionaries/ for .ifo files and populate the internal list.
  // Existing enabled/order state from a prior loadConfig() is preserved for
  // dicts that are still present; newly found dicts are added enabled=true.
  void scan();

  // Persist enabled/order state to /.crosspoint/dict_config.json.
  bool saveConfig() const;

  // Load enabled/order state from /.crosspoint/dict_config.json.
  bool loadConfig();

  // After scan()+loadConfig(), call this to ensure RAM is used only for
  // enabled dictionaries: loads checkpoints for enabled entries (from .chk
  // cache if valid, otherwise builds and saves), and frees checkpoints for
  // disabled entries.
  void syncCheckpointsToEnabled();

  // Look up a word across all enabled dictionaries (in order).
  // Writes at most maxLen-1 bytes into `definition` (always null-terminated).
  // Returns true if a definition was found.
  // Optional bookLang (e.g. "tr", "en-US") filters dicts by language; if a
  // dict has no lang info it is always tried.
  bool lookup(const char* word, char* definition, size_t maxLen,
              const char* bookLang = nullptr) const;

  // All known dictionaries in their current display/search order.
  const std::vector<DictEntry>& getEntries() const { return entries; }
  std::vector<DictEntry>& getEntries() { return entries; }

  // Move a dict up/down in the list (for settings UI).
  void moveUp(size_t index);
  void moveDown(size_t index);

  // Toggle enabled state.
  void setEnabled(size_t index, bool enabled);

  size_t count() const { return entries.size(); }

 private:
  DictionaryStore() = default;

  static DictionaryStore instance;
  std::vector<DictEntry> entries;

  static constexpr char CONFIG_PATH[] = "/.crosspoint/dict_config.json";
  static constexpr char DICT_ROOT[] = "/dictionaries";
};

#define DICT_STORE DictionaryStore::getInstance()
