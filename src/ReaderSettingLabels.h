#pragma once

#include <I18n.h>

#include <array>

#include "CrossPointSettings.h"

// Shared by the full settings screen, quick settings and reader overlays.
namespace ReaderSettingLabels {
inline constexpr std::array builtinFonts = {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS};
inline constexpr std::array bionic = {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_SUBTLE};
inline constexpr std::array refresh = {StrId::STR_PAGES_1,  StrId::STR_PAGES_5,  StrId::STR_PAGES_10,
                                       StrId::STR_PAGES_15, StrId::STR_PAGES_30, StrId::STR_NEVER};
static_assert(builtinFonts.size() == CrossPointSettings::BUILTIN_FONT_COUNT);
static_assert(bionic.size() == CrossPointSettings::BIONIC_READING_MODE_COUNT);
static_assert(refresh.size() == CrossPointSettings::REFRESH_FREQUENCY_COUNT);
}  // namespace ReaderSettingLabels
