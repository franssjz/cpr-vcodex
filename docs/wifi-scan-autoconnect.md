# Wi-Fi Scan-First + Auto-Connect Integration Guide

This guide describes the two Wi-Fi UX changes made in this fork and how to re-apply them to any upstream update.

---

## What Changed and Why

The upstream firmware (`franssjz/crosspoint-reader`) auto-connects to the last-known SSID on entry. If that network is not in range, it times out (15 s), then falls back to scanning. The result is a slow, frustrating experience when the usual network is unavailable.

This fork replaces that behaviour with:

1. **Scan first, always.** Every Wi-Fi entry point scans before attempting any connection. The user is never blocked by a stale SSID.
2. **Auto-connect from scan results.** If the scan finds a network for which a saved password exists, the firmware silently connects to the strongest one. The user only sees the list when no saved network is in range.

| Scenario | Behaviour |
|---|---|
| Saved network in range | Scan → auto-connect (no list shown) |
| Multiple saved networks in range | Scan → connect to strongest RSSI |
| No saved networks in range | Scan → show list for manual selection |
| Manual pick of known SSID | Silent connect (saved password re-used) |
| Manual pick of unknown SSID | Password prompt |
| Connection fails on saved password | `CONNECTION_FAILED` → offer "Forget network?" |

---

## Change 1 — Disable SDK NVS Auto-Reconnect at Boot

**File:** `src/main.cpp`

**Where:** In `setup()`, immediately after `powerManager.begin()`.

**What to add:**

```cpp
#include <WiFi.h>    // Add to the includes block near the top of main.cpp

// In setup(), after powerManager.begin():
WiFi.persistent(false);  // suppress SDK's hidden nvs.net80211 auto-connect
WiFi.mode(WIFI_OFF);     // keep radio off until user reaches WifiSelectionActivity
```

**Why:** The ESP-IDF SDK stores Wi-Fi credentials in a hidden NVS partition (`nvs.net80211`). Even before the activity system starts, the SDK can reconnect automatically on boot. `WiFi.persistent(false)` prevents the SDK from writing to NVS going forward. `WiFi.mode(WIFI_OFF)` ensures no radio activity happens at boot. Credentials are managed entirely by `WifiCredentialStore` (SD card JSON), not NVS.

**Verification:** After flashing, the device should boot with Wi-Fi radio off. Opening a Wi-Fi-gated activity should always show the scanning screen first, never an instant connection.

---

## Change 2 — Default `autoConnect` to `false`

**File:** `src/activities/network/WifiSelectionActivity.h`

**Where:** Constructor declaration (search for `explicit WifiSelectionActivity`).

**Before:**
```cpp
explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = true)
```

**After:**
```cpp
explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = false)
```

**Why:** All 7 implicit call sites throughout the codebase pass no third argument. Changing the default from `true` to `false` makes every Wi-Fi entry point scan-first without touching any call site. The `allowAutoConnect` branch in `onEnter()` still exists but is never triggered by normal usage.

**Verification:** Search for `WifiSelectionActivity(` in `src/` — every call site that omits the third argument now behaves scan-first.

---

## Change 3 — Auto-Connect to Best Saved Network After Scan

**File:** `src/activities/network/WifiSelectionActivity.cpp`

**Where:** `processWifiScanResults()`, after `WiFi.scanDelete()`.

The networks vector is sorted by the existing code: saved-password networks first, then by RSSI descending. The first entry is therefore always the strongest saved network in range (if any).

**Before (end of function):**
```cpp
  WiFi.scanDelete();
  state = WifiSelectionState::NETWORK_LIST;
  selectedNetworkIndex = 0;
  requestUpdate();
}
```

**After:**
```cpp
  WiFi.scanDelete();

  // Auto-connect to the best saved-password network if one is in range.
  // Networks are already sorted: saved-password first, then by RSSI descending,
  // so networks[0] is always the best candidate.
  if (!networks.empty() && networks[0].hasSavedPassword) {
    LOG_DBG("WIFI", "Found saved network in range: %s (RSSI: %d) - auto-connecting",
            networks[0].ssid.c_str(), networks[0].rssi);
    selectedNetworkIndex = 0;
    selectNetwork(0);
    return;
  }

  state = WifiSelectionState::NETWORK_LIST;
  selectedNetworkIndex = 0;
  requestUpdate();
}
```

**Why this works without other changes:**
- `selectNetwork(0)` already checks `WIFI_STORE.findCredential()` and calls `attemptConnection()` silently — this path already existed for the "user picks a known SSID" case.
- `usedSavedPassword = true` is set inside `selectNetwork()`, which means `checkConnectionStatus()` will offer "Forget network?" if the connection fails — same as before.
- No new state or flag is needed.

**Verification:**
1. Save a Wi-Fi password for your home network.
2. Trigger any Wi-Fi-gated feature (web server, OTA, etc.).
3. You should see: "Scanning" → "Connecting to \<SSID\>" → connected. The network list is never shown.
4. Move to a location with no saved networks. You should see: "Scanning" → network list.

---

## Porting Checklist

- [ ] Add `#include <WiFi.h>` to `src/main.cpp` includes (if not already present)
- [ ] Add `WiFi.persistent(false);` + `WiFi.mode(WIFI_OFF);` after `powerManager.begin()` in `setup()`
- [ ] Change `autoConnect = true` → `autoConnect = false` in `WifiSelectionActivity.h` constructor
- [ ] Add auto-connect block after `WiFi.scanDelete();` in `processWifiScanResults()`
- [ ] Build: `pio run` — zero errors/warnings expected
- [ ] Flash and verify scan-first + auto-connect on device
