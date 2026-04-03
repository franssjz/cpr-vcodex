// Mock JsonSettingsIO.h for native testing
#pragma once

class CrossPointSettings;

namespace JsonSettingsIO {

inline bool saveSettings(const CrossPointSettings&, const char*) { return true; }
inline bool loadSettings(CrossPointSettings&, const char*, bool* resave = nullptr) {
  if (resave) *resave = false;
  return true;
}

}  // namespace JsonSettingsIO
