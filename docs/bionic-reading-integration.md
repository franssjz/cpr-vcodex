# Bionic Reading (Metaguiding) Integration Guide

This guide explains how bionic reading / metaguiding works in the cpr-vcodex firmware, how its two integration paths interact, and how to port the feature to any Xteink-based firmware fork.

---

## What Is Bionic Reading?

Bionic reading (also called metaguiding or intellireading) bolds the first portion of each word, letting the eye anchor on the bold prefix and the brain fill in the rest. On e-ink, where rendering is slow and the reading surface is uniform, the anchor points reduce eye fatigue over long sessions.

---

## Two Independent Integration Paths

cpr-vcodex ships **both** approaches. They are independent and can coexist.

| | Path A — Firmware Runtime | Path B — Calibre Plugin Pre-process |
|---|---|---|
| Where it runs | On-device, at draw time | Desktop (Calibre), before file transfer |
| EPUB modified? | No | Yes (`<b>` tags injected) |
| Toggle on/off | Instant (no re-render of cache) | Requires re-running the plugin |
| Works on any EPUB | Yes | Yes |
| Hardware required | Firmware with the feature | Any EPUB reader |
| Recommended for | All users | Calibre power users who want device-agnostic bolding |

**Which to use:**

- If your firmware implements Path A (runtime), users never need the Calibre plugin. The firmware handles any unmodified EPUB.
- Path B is useful if users want metaguided text on devices or apps that do not implement runtime bionic reading.
- If a user runs both (pre-processed EPUB + firmware runtime ON), no double-bolding occurs — see [How the Two Paths Interact](#how-the-two-paths-interact).

---

## The Algorithm

Both the firmware and the Calibre plugin use the same midpoint formula, originally from `metaguiding.py:_bold_word()`:

```
M = 1           if word_length ∈ {1, 3}
M = ⌈N / 2⌉    otherwise
```

where `M` is the number of leading characters to bold and `N` is the codepoint count of the word.

**Examples:**

| Word | N | M | Rendered |
|------|---|---|---------|
| I | 1 | 1 | **I** |
| at | 2 | 1 | **a**t |
| the | 3 | 1 | **t**he |
| read | 4 | 2 | **re**ad |
| quick | 5 | 3 | **qui**ck |
| reader | 6 | 3 | **rea**der |
| reading | 7 | 4 | **read**ing |
| firmware | 8 | 4 | **firm**ware |

**Word boundary definition** (both implementations):

- ASCII: `[A-Za-z0-9_]`
- Non-ASCII: all bytes ≥ `0x80` (i.e., any UTF-8 continuation or lead byte)
- Punctuation, spaces, and HTML entities are skipped and drawn in the original style

---

## Path A — Firmware Runtime Integration

This is a step-by-step guide for adding runtime bionic reading to an Xteink firmware fork.

### Step 1 — Settings Field

In your settings struct, add a boolean toggle. In cpr-vcodex this lives at `src/CrossPointSettings.h:247`:

```cpp
uint8_t bionicReading = 0;
```

### Step 2 — Settings Registration

Register the toggle in `SettingsList.cpp` under the reader category (`src/SettingsList.cpp:56-57`):

```cpp
SettingInfo::Toggle(StrId::STR_BIONIC_READING, &CrossPointSettings::bionicReading,
                    "bionicReading", StrId::STR_CAT_READER),
```

### Step 3 — JSON Serialization

In your settings IO file (`src/JsonSettingsIO.cpp`):

```cpp
// Load (line 269):
loadToggle("bionicReading", s.bionicReading);

// Save (line 535):
doc["bionicReading"] = s.bionicReading;
```

### Step 4 — i18n String

Add `STR_BIONIC_READING` to every translation YAML under `lib/I18n/translations/`, then regenerate:

```bash
python scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

### Step 5 — TextBlock Renderer

This is the core of the feature. Add bionic rendering to `TextBlock::render()`. The full implementation is in `lib/Epub/Epub/blocks/TextBlock.cpp:1-132`.

Key helpers at the top of the file:

```cpp
// Midpoint formula — faithful port of metaguiding.py:78
static constexpr int bionicMidpoint(int n) {
  return (n == 1 || n == 3) ? 1 : (n + 1) / 2;
}

// Count UTF-8 codepoints by skipping continuation bytes
static int utf8CodepointCount(const char* begin, const char* end) {
  int n = 0;
  for (const char* p = begin; p < end; ++p) {
    if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) ++n;
  }
  return n;
}

// \w under re.UNICODE: ASCII alnum/underscore + all non-ASCII UTF-8 bytes
static inline bool isWordByte(uint8_t b) {
  if (b >= 0x80) return true;
  return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b == '_');
}
```

The render function signature gains a `bionicReading` parameter:

```cpp
void TextBlock::render(const GfxRenderer& renderer, const int fontId,
                       const int x, const int y, const bool bionicReading) const;
```

**Critical fast paths** (`TextBlock.cpp:43-46`):

```cpp
const bool alreadyBold = (currentStyle & EpdFontFamily::BOLD) != 0;
if (!bionicReading || alreadyBold || w.size() >= 128) {
  renderer.drawText(fontId, wordX, y, w.c_str(), true, currentStyle);
}
```

Three cases bypass bionic rendering:
1. **`bionicReading` is off** — draw normally.
2. **Word is already bold** — respect existing bold markup (critical for plugin interop; see [How the Two Paths Interact](#how-the-two-paths-interact)).
3. **Word is ≥ 128 bytes** — avoids overflowing the stack buffer.

For words that need bionic splitting, a 128-byte stack buffer is used — no heap allocations in the render path:

```cpp
char buf[128];
// ... walk word bytes, split at M-th codepoint, draw bold prefix + regular suffix
```

The per-word loop:
1. Draw any leading non-word bytes (punctuation) in the original style.
2. Find the word run (`isWordByte` scan).
3. Count its codepoints → compute `M = bionicMidpoint(ncp)`.
4. Walk bytes to find the byte offset of the M-th codepoint.
5. Copy prefix into `buf`, draw with `BOLD` style.
6. Copy suffix into `buf`, draw with original style.
7. Advance cursor by `getTextAdvanceX()` after each segment.

### Step 6 — Render Pipeline Propagation

Thread the flag through every layer between `EpubReaderActivity` and `TextBlock`.

**`Page.cpp:53-58`** — `Page::render` fans out to each element:

```cpp
void Page::render(GfxRenderer& renderer, const int fontId,
                  const int xOffset, const int yOffset, const bool bionicReading) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset, bionicReading);
  }
}
```

**`Page.cpp:6-8`** — `PageLine::render` passes it to `TextBlock`:

```cpp
void PageLine::render(GfxRenderer& renderer, const int fontId,
                      const int xOffset, const int yOffset, const bool bionicReading) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset, bionicReading);
}
```

**`PageImage::render`** accepts but ignores `bionicReading` — images are unaffected.

**`EpubReaderActivity`** passes the live setting at the call site (`src/activities/reader/EpubReaderActivity.cpp:964`):

```cpp
page->render(renderer, SETTINGS.getReaderFontId(),
             orientedMarginLeft, orientedMarginTop, SETTINGS.bionicReading);
```

### Step 7 — Section Cache: Do NOT Add `bionicReading` to the Header

The section cache stores the pre-computed page layout (where line breaks fall, image positions, etc.). This layout depends on font metrics and viewport size — **not** on whether word prefixes are bold.

`Section::writeSectionFileHeader()` and `Section::loadSectionFile()` validate these fields (`lib/Epub/Epub/Section.cpp:36-112`):

| Field in header | Invalidates cache if changed? |
|---|---|
| `SECTION_FILE_VERSION` | Yes |
| `fontId` | Yes |
| `lineCompression` | Yes |
| `extraParagraphSpacing` | Yes |
| `paragraphAlignment` | Yes |
| `viewportWidth` / `viewportHeight` | Yes |
| `hyphenationEnabled` | Yes |
| `embeddedStyle` | Yes |
| `imageRendering` | Yes |
| `bionicReading` | **No — intentionally absent** |

Toggling bionic reading is instant: the cached page layout is reused, and the new bold/regular split is applied at draw time.

If you add `bionicReading` to the cache header by mistake, every toggle will invalidate all section caches, forcing a multi-second re-parse of every EPUB the user opens.

---

## Path B — Calibre Plugin (intellireader)

The `intellireader/` directory in this repository is a standalone Calibre plugin (by Hugo Batista, GPL v3). It pre-processes EPUBs before they reach the device.

### What the Plugin Does

1. Opens the EPUB as a ZIP archive.
2. For each XHTML/HTML content file (skipping `nav.xhtml`, `toc.xhtml`, and other TOC files):
   - Finds every text node in the `<body>` using a regex.
   - Splits each word at the midpoint and wraps the prefix in `<b>` tags.
3. Writes an `intellireading.metaguide` marker file to the ZIP root (prevents double-processing).
4. Saves the pre-metaguided version back to Calibre; backs up the original as `ORIGINAL_EPUB`.

**Before:**
```html
<p>The quick brown fox jumps over the lazy dog.</p>
```

**After:**
```html
<p><b>T</b>he <b>qu</b>ick <b>br</b>own <b>f</b>ox <b>ju</b>mps <b>ov</b>er <b>t</b>he <b>l</b>azy <b>d</b>og.</p>
```

No CSS is added. The plugin relies on the reader's default rendering of `<b>` (bold font weight).

### Installation

1. **Calibre ≥ 8.4.0** is required.
2. In Calibre: **Preferences → Plugins → Load plugin from file** → select the `intellireader/` directory zipped as a `.zip`.
3. Restart Calibre. A toolbar button appears; keyboard shortcut is **Shift+M**.
4. One configurable option: default action is EPUB or KEPUB (set in plugin preferences).

### Usage

1. Select one or more books in the Calibre library.
2. Click the toolbar button (or press **Shift+M**).
3. The plugin backs up the original as `ORIGINAL_EPUB`, then replaces the format with the metaguided version.
4. Transfer the book to the device normally.

### Known Limitations

- **Remove metaguiding is experimental** — the warning dialog says so. Prefer reverting via Calibre's `ORIGINAL_EPUB` backup.
- Hyphenated words split at the hyphen: `e-book` → `**e**-**b**ook`.
- HTML entity references within words (e.g., `r&eacute;sum&eacute;`) may produce incorrect splits.
- The entire EPUB is held in memory during processing (`BytesIO`); very large files may be slow.
- Navigation files are detected heuristically by filename (`nav.xhtml`, `toc.xhtml`, etc.) — unusual filenames may be incorrectly processed.

---

## How the Two Paths Interact

The firmware's fast path (`TextBlock.cpp:44`) skips any word that is **already bold**:

```cpp
const bool alreadyBold = (currentStyle & EpdFontFamily::BOLD) != 0;
if (!bionicReading || alreadyBold || ...) {
  renderer.drawText(...);  // draw as-is
}
```

When a user has both firmware runtime bionic ON and a plugin-pre-processed EPUB:

- Words that were bolded by the plugin (`<b>prefix</b>suffix`) arrive at `TextBlock` with `EpdFontFamily::BOLD` set on the prefix word. The firmware's fast path fires — they are drawn bold without re-splitting.
- Words that were not processed (plugin skipped their container, e.g., a section added after plugin processing) get runtime bionic applied by the firmware.
- **Result: no double-bolding. Both paths are safe to use simultaneously.**

If firmware runtime bionic is **OFF**, plugin-pre-processed EPUBs still show metaguided text because the `<b>` tags are embedded in the HTML and the EPUB renderer renders them as bold naturally.

---

## Porting Checklist

Use this as a quick checklist when adding the feature to a different Xteink firmware fork:

- [ ] Add `uint8_t bionicReading = 0;` to the settings struct
- [ ] Register `SettingInfo::Toggle(STR_BIONIC_READING, ...)` in `SettingsList`
- [ ] Add `loadToggle` / `doc["bionicReading"]` in settings IO
- [ ] Add `STR_BIONIC_READING` to translation YAMLs; run `gen_i18n.py`
- [ ] Add `bionicMidpoint()`, `utf8CodepointCount()`, `isWordByte()` helpers to `TextBlock.cpp`
- [ ] Add `bool bionicReading` parameter to `TextBlock::render()`
- [ ] Add fast path: skip if `!bionicReading || alreadyBold || w.size() >= 128`
- [ ] Implement word-split loop with 128-byte stack buffer
- [ ] Add `bool bionicReading` parameter to `PageLine::render()` and `Page::render()`
- [ ] Pass `SETTINGS.bionicReading` from the reader activity to `page->render()`
- [ ] Confirm `bionicReading` is **absent** from the section cache header and validation
- [ ] Build and verify: `pio run` with zero errors/warnings

---

## Verification

**Build:**

```bash
pio run -t clean && pio run
```

Zero errors and zero warnings expected.

**Visual test on device:**

1. Open any EPUB with standard English prose.
2. Go to **Settings → Reader → Bionic Reading** and toggle it on.
3. Navigate to a text page. Every word should show a bold prefix followed by a lighter suffix.
4. Toggle off. The page re-renders with no bold prefixes.
5. There should be no visible lag — the section cache is reused.

**Edge cases to test manually:**

| Input | Expected output |
|---|---|
| Single character word ("I") | Entire word bold |
| 3-character word ("the") | First character bold only |
| Multi-byte UTF-8 (e.g., "réalité") | Bold prefix counted in codepoints, not bytes |
| Already-bold run (existing `<b>` in EPUB) | Drawn as-is; no re-split |
| Word ≥ 128 bytes (pathological) | Drawn as-is; no crash |

**Calibre plugin verification:**

After running the plugin on an EPUB, inspect the output:

```bash
unzip -l book.epub | grep intellireading
# Should show: intellireading.metaguide
```

Open an XHTML file from the archive and confirm `<b>` tags appear around word prefixes.

**Serial heap check:**

The bionic render path allocates zero heap. Confirm with:

```cpp
LOG_DBG("MEM", "Free heap: %d", ESP.getFreeHeap());
```

Heap usage should be identical before and after toggling bionic reading.
