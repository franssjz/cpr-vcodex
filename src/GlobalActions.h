#pragma once

#include "CrossPointSettings.h"

inline bool isPowerButtonActionAvailableOutsideReader(const CrossPointSettings::SHORT_PWRBTN action) {
  switch (action) {
    case CrossPointSettings::SHORT_PWRBTN::SLEEP:
    case CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH:
    case CrossPointSettings::SHORT_PWRBTN::SCREENSHOT:
    case CrossPointSettings::SHORT_PWRBTN::FILE_TRANSFER:
      return true;
    case CrossPointSettings::SHORT_PWRBTN::IGNORE:
    case CrossPointSettings::SHORT_PWRBTN::PAGE_TURN:
    case CrossPointSettings::SHORT_PWRBTN::SHORT_PWRBTN_COUNT:
    default:
      return false;
  }
}
