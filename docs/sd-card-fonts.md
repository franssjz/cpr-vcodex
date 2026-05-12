# SD Card Fonts

CPR-vCodex supports loading additional fonts from the SD card, including fonts
with extended Unicode coverage (CJK, Cyrillic, Greek, etc.).

## Installing Fonts

There are three ways to install fonts:

### Option 1: Download from device (recommended)

1. Connect your CPR-vCodex reader to WiFi
2. Go to **Settings > Reader > Manage Fonts**
3. Browse available font families and tap to download
4. Downloaded fonts appear immediately in **Settings > Reader > Font Family**

### Option 2: Upload via web browser

1. Connect your CPR-vCodex reader to WiFi
2. Open the web interface in your browser (shown on the WiFi screen)
3. Navigate to the **Fonts** tab
4. Upload `.cpfont` files using the upload form

### Option 3: Manual SD card copy

1. Download the `.cpfont` files for the family you want from the
   [CPR-vCodex SD font release](https://github.com/franssjz/cpr-vcodex/releases/tag/sd-fonts-m1-b4)
2. Create a folder with the family name and copy its `.cpfont` files to one of
   two locations on your SD card:

   - `/.fonts/` — hidden directory (preferred; keeps the SD root tidy
     when mounted on a desktop)
   - `/fonts/` — visible directory (use this if your OS hides dot-files
     and you'd rather see the folder in your file manager)

   Both roots are always scanned at boot and the results are merged: a
   family installed in `/fonts/` shows up even when `/.fonts/` also
   exists, and vice versa. The two roots only collide if the same family
   name appears in both — in that case the copy in `/.fonts/` wins and
   the duplicate in `/fonts/` is ignored.

       SD Card Root/
       ├── .fonts/                     ← Hidden root (preferred)
       │   └── Literata/
       │       ├── Literata_12.cpfont
       │       ├── Literata_14.cpfont
       │       ├── Literata_16.cpfont
       │       └── Literata_18.cpfont
       └── fonts/                      ← Visible root (equally valid)
           └── Merriweather/
               ├── Merriweather_12.cpfont
               └── ...

3. Insert the SD card and power on your CrossPoint reader

## Available Pre-Built Fonts

The current list of pre-built fonts is maintained in
`lib/EpdFont/scripts/sd-fonts.yaml` and published as CPR-vCodex release assets:

- Stable device manifest: https://github.com/franssjz/cpr-vcodex/releases/tag/sd-fonts-m1-b4
- The firmware downloads: https://github.com/franssjz/cpr-vcodex/releases/download/sd-fonts-m1-b4/fonts.json

The `sd-fonts-m<META>-b<BIN>` tag is tied to the manifest schema and `.cpfont`
binary format supported by the firmware. When either format changes, update the
version constants and publish a new SD font release.

## Converting Custom Fonts

To convert your own TrueType/OpenType fonts:

### Prerequisites

    pip install freetype-py fonttools

### Single font (one style)

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyFont-Regular.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --style regular \
      --name MyFont \
      --output-dir ./MyFont/

### Multi-style font

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      --regular MyFont-Regular.ttf \
      --bold MyFont-Bold.ttf \
      --italic MyFont-Italic.ttf \
      --bolditalic MyFont-BoldItalic.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --name MyFont \
      --output-dir ./MyFont/

### Available Unicode interval presets

| Preset | Coverage |
|--------|----------|
| `ascii` | U+0020-U+007E (Basic Latin) |
| `latin-ext` | European languages (Latin + Extended-A/B) |
| `greek` | Greek + Extended Greek |
| `cyrillic` | Cyrillic + Supplement |
| `cjk` | CJK Unified Ideographs + Hiragana + Katakana + Fullwidth |
| `hangul` | Korean Hangul syllables |
| `builtin` | Matches built-in Bookerly coverage exactly |

Combine presets with commas: `--intervals latin-ext,greek,cyrillic`

Install custom fonts via WiFi upload or manual SD card copy.
