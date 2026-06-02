#include "TimeZoneRegistry.h"

#include <array>

namespace {
constexpr std::array<TimeZonePreset, 30> TIME_ZONE_PRESETS = {{
    {"Europe/Madrid (Madrid)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"UTC (UTC)", "UTC0"},
    {"Europe/London (London)", "GMT0BST,M3.5.0/1,M10.5.0/2"},
    {"Europe/Paris (Paris)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"Europe/Berlin (Berlin)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"Europe/Rome (Rome)", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"Europe/Athens (Athens)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki (Helsinki)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul (Istanbul)", "TRT-3"},
    {"Europe/Moscow (Moscow)", "MSK-3"},
    {"America/New_York (New York)", "EST5EDT,M3.2.0/2,M11.1.0/2"},
    {"America/Chicago (Chicago)", "CST6CDT,M3.2.0/2,M11.1.0/2"},
    {"America/Denver (Denver)", "MST7MDT,M3.2.0/2,M11.1.0/2"},
    {"America/Los_Angeles (Los Angeles)", "PST8PDT,M3.2.0/2,M11.1.0/2"},
    {"America/Mexico_City (Mexico City)", "CST6"},
    {"America/Sao_Paulo (Sao Paulo)", "BRT3"},
    {"America/Buenos_Aires (Buenos Aires)", "ART3"},
    {"Africa/Johannesburg (Johannesburg)", "SAST-2"},
    {"Africa/Nairobi (Nairobi)", "EAT-3"},
    {"Asia/Dubai (Dubai)", "GST-4"},
    {"Asia/Karachi (Karachi)", "PKT-5"},
    {"Asia/Kolkata (New Delhi)", "IST-5:30"},
    {"Asia/Dhaka (Dhaka)", "BDT-6"},
    {"Asia/Bangkok (Bangkok)", "ICT-7"},
    {"Asia/Singapore (Singapore)", "SGT-8"},
    {"Asia/Shanghai (Beijing)", "CST-8"},
    {"Asia/Tokyo (Tokyo)", "JST-9"},
    {"Asia/Seoul (Seoul)", "KST-9"},
    {"Australia/Sydney (Sydney)", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
    {"Pacific/Auckland (Auckland)", "NZST-12NZDT,M9.5.0/2,M4.1.0/3"},
}};
}  // namespace

size_t TimeZoneRegistry::getPresetCount() { return TIME_ZONE_PRESETS.size(); }

uint8_t TimeZoneRegistry::clampPresetIndex(const uint8_t index) {
  return index < TIME_ZONE_PRESETS.size() ? index : DEFAULT_TIME_ZONE_INDEX;
}

const TimeZonePreset& TimeZoneRegistry::getPreset(const uint8_t index) {
  return TIME_ZONE_PRESETS[clampPresetIndex(index)];
}

const char* TimeZoneRegistry::getPresetLabel(const uint8_t index) { return getPreset(index).label; }

const char* TimeZoneRegistry::getPresetPosixTz(const uint8_t index) { return getPreset(index).posixTz; }
