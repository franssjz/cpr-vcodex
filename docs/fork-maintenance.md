# Fork Maintenance Guide

This document explains how to keep this fork in sync with `franssjz/crosspoint-reader` (upstream) and how to re-apply the custom features after each upstream update.

---

## Custom Features in This Fork

| Feature | Guide | Commit |
|---|---|---|
| Bionic Reading (runtime metaguiding) | [docs/bionic-reading-integration.md](bionic-reading-integration.md) | `a9c4291` |
| Wi-Fi Scan-First + Auto-Connect | [docs/wifi-scan-autoconnect.md](wifi-scan-autoconnect.md) | `fe8d6d9`, `8874835` |

Both features are isolated to a small set of files and have minimal overlap with each other or with upstream code, so merge conflicts are rare.

---

## One-Time Setup — Add the Upstream Remote

```bash
git remote add upstream https://github.com/franssjz/crosspoint-reader.git
git remote -v
# origin    https://github.com/<your-username>/cpr-vcodex.git
# upstream  https://github.com/franssjz/crosspoint-reader.git
```

---

## Merging an Upstream Update

### Step 1 — Fetch and review upstream changes

```bash
git fetch upstream
git log upstream/master --oneline --not master
# Lists commits in upstream that you don't have yet
```

Read the commit messages before merging — upstream may touch files that the custom features also modify (see [Conflict-Prone Files](#conflict-prone-files)).

### Step 2 — Merge

```bash
git merge upstream/master
```

Most merges will complete cleanly. If there are conflicts, see [Resolving Conflicts](#resolving-conflicts) below.

### Step 3 — Verify the custom features still work

After any merge, run through both checklists:

**Bionic Reading** (quick sanity check):
- [ ] `src/CrossPointSettings.h` still has `uint8_t bionicReading = 0;`
- [ ] `lib/Epub/Epub/blocks/TextBlock.cpp` still has the `bionicMidpoint()` helper and the bold-split loop
- [ ] `lib/Epub/Epub/Page.h` / `Page.cpp` still pass `bool bionicReading` through the render chain
- [ ] `lib/I18n/translations/english.yaml` still has `STR_BIONIC_READING`
- [ ] `src/SettingsList.cpp` still registers the toggle

**Wi-Fi**:
- [ ] `src/main.cpp` still has `WiFi.persistent(false);` + `WiFi.mode(WIFI_OFF);` in `setup()`
- [ ] `WifiSelectionActivity.h` constructor still defaults `autoConnect = false`
- [ ] `WifiSelectionActivity.cpp:processWifiScanResults()` still has the auto-connect block after `WiFi.scanDelete()`

### Step 4 — Build and flash

```bash
pio run -t clean && pio run
```

Zero errors/warnings expected. Flash the result and smoke-test both features on device.

---

## Conflict-Prone Files

These files are most likely to have merge conflicts:

| File | Why |
|---|---|
| `src/main.cpp` | Boot sequence — our `WiFi.persistent/mode` lines vs upstream changes to `setup()` |
| `src/CrossPointSettings.h` | Settings struct — upstream adds fields; we add `bionicReading` |
| `src/SettingsList.cpp` | Settings list — upstream adds items; we add bionic toggle |
| `src/JsonSettingsIO.cpp` | JSON load/save — upstream adds keys; we add `bionicReading` |
| `lib/Epub/Epub/blocks/TextBlock.cpp` | Core render — upstream might refactor; we added bionic split |
| `src/activities/network/WifiSelectionActivity.cpp` | Wi-Fi logic — our auto-connect block |
| `src/activities/network/WifiSelectionActivity.h` | Constructor default |

### Resolving Conflicts

Git will mark conflicts with `<<<<<<< HEAD` / `>>>>>>> upstream/master`. For each conflict:

1. Open the file in your editor.
2. Keep **both** sides: upstream changes AND your custom additions.
3. Use the feature guides linked above as the source of truth for what your code should look like.
4. After resolving: `git add <file>` then `git merge --continue`.

Example — `src/main.cpp` conflict in `setup()`:

```cpp
// upstream might add new init call here:
newUpstreamComponent.begin();

// your lines must stay:
WiFi.persistent(false);
WiFi.mode(WIFI_OFF);
```

Keep both; order typically does not matter as long as the Wi-Fi lines come after `powerManager.begin()`.

---

## When a Re-Implementation Is Needed

If upstream significantly refactors a conflict-prone file (e.g., replaces `WifiSelectionActivity` entirely or rewrites `TextBlock`), merging may not be practical. In that case:

1. Take upstream's new version of the file as the base.
2. Follow the step-by-step porting checklist in the relevant feature guide.
3. You can give the guide to an AI assistant along with the new upstream file and ask it to apply the changes — the guides are written for exactly this purpose.

---

## Releasing After a Merge

This fork uses a version tag scheme: `1.2.0.<N>` where `N` continues from franssjz's last release number.

After merging and verifying on device:

```bash
# Tag the new release
git tag 1.2.0.<next>
git push origin master
git push origin 1.2.0.<next>
```

Then create a GitHub Release on the fork with only `firmware.bin` attached (flashable at https://xteink.dve.al/).

---

## Reference: Files Changed by Each Feature

### Bionic Reading

| File | Change |
|---|---|
| `src/CrossPointSettings.h` | `uint8_t bionicReading = 0;` |
| `src/SettingsList.cpp` | Toggle registration |
| `src/JsonSettingsIO.cpp` | load/save |
| `lib/I18n/translations/english.yaml` | `STR_BIONIC_READING` string |
| `lib/Epub/Epub/blocks/TextBlock.h` | `render()` signature |
| `lib/Epub/Epub/blocks/TextBlock.cpp` | Algorithm + split loop |
| `lib/Epub/Epub/Page.h` | `bionicReading` param in signatures |
| `lib/Epub/Epub/Page.cpp` | Pass-through in render chain |
| `src/activities/reader/EpubReaderActivity.cpp` | 5 call sites pass `SETTINGS.bionicReading` |

### Wi-Fi Scan-First + Auto-Connect

| File | Change |
|---|---|
| `src/main.cpp` | `WiFi.persistent(false)` + `WiFi.mode(WIFI_OFF)` in `setup()` |
| `src/activities/network/WifiSelectionActivity.h` | `autoConnect = false` default |
| `src/activities/network/WifiSelectionActivity.cpp` | Auto-connect block in `processWifiScanResults()` |
