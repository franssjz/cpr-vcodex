#pragma once

#include <vector>

#include "activities/settings/SettingsActivity.h"

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
const std::vector<SettingInfo>& getSettingsList();
