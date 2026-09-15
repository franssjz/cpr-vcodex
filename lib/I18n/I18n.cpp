#include "I18n.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstring>

#include "I18nStrings.h"

using namespace i18n_strings;

// Settings file path
static constexpr const char* SETTINGS_FILE = "/.crosspoint/language.bin";
static constexpr uint8_t SETTINGS_VERSION = 1;

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(StrId id) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::_COUNT)) {
    return "???";
  }

  const LangStrings lang = getLanguageStrings(_language);
  const uint16_t off = lang.offsets[index];
  if (off & 0x8000) {
    return STRINGS_EN_DATA + (off & 0x7FFF);
  }
  return lang.data + off;
}

void I18n::setLanguage(Language lang) {
  if (lang >= Language::_COUNT) {
    return;
  }
  _language = lang;
}

const char* I18n::getLanguageName(Language lang) const {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::_COUNT)) {
    return "???";
  }
  return LANGUAGE_NAMES[index];
}

Language I18n::languageFromCode(const char* code) {
  if (!code) return Language::EN;
  for (uint8_t i = 0; i < getLanguageCount(); i++) {
    if (strcmp(code, LANGUAGE_CODES[i]) == 0) {
      return static_cast<Language>(i);
    }
  }
  return Language::EN;
}

bool I18n::loadSettings() {
  HalFile file;
  if (!Storage.openFileForRead("I18N", SETTINGS_FILE, file)) {
    LOG_DBG("I18N", "No settings file, using default (English)");
    return false;
  }

  uint8_t data[2] = {};
  if (file.read(data, sizeof(data)) != sizeof(data) || data[0] != SETTINGS_VERSION) {
    LOG_ERR("I18N", "Invalid legacy language file");
    return false;
  }
  // CPR releases through 1.5.0.30 stored the _order ordinal, not the
  // BCP47-sorted enum. Vietnamese was appended after upstream's V1 table.
  if (data[1] < V1_LANGUAGE_COUNT) {
    _language = V1_LANGUAGES[data[1]];
  } else if (data[1] == V1_LANGUAGE_COUNT) {
    _language = Language::VI;
  } else {
    LOG_ERR("I18N", "Invalid legacy language index: %u", data[1]);
    return false;
  }
  return true;
}

// Generate character set for a specific language
const char* I18n::getCharacterSet(Language lang) {
  const auto langIndex = static_cast<size_t>(lang);
  if (langIndex >= static_cast<size_t>(Language::_COUNT)) {
    lang = Language::EN;  // Fallback to first language
  }

  return CHARACTER_SETS[static_cast<size_t>(lang)];
}
