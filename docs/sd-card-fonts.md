# SD Card Fonts

CPR-vCodex supports loading additional fonts from the SD card. Common
downloadable families are provided by CrossPoint, while the CPR-vCodex source
provides vCodex-specific additions such as ChareInk and Lexend.

## Installing Fonts

There are three ways to install `.cpfont` fonts. Direct TTF/OTF/TTC loading
requires external RAM and uses the manual SD copy method. X3/X4 require `.cpfont`.

### Option 1: Download from device

1. Connect your CPR-vCodex reader to Wi-Fi
2. Go to **Settings > Reader > Manage Fonts**
3. Browse available font families and tap to download
4. Downloaded fonts appear immediately in **Settings > Reader > Font Family**

### Option 2: Upload via web browser

1. On your CPR-vCodex reader, start **File Transfer** and connect through **Join Network** or **Create Hotspot**
2. Open the web interface URL shown on the reader
3. Navigate to the **Fonts** tab
4. Upload `.cpfont` files using the upload form

### Option 3: Manual SD card copy

For the fastest full CPR-vCodex font install, download
[`all-fonts.zip`](https://github.com/franssjz/cpr-vcodex/releases/download/sd-fonts-m1-b4/all-fonts.zip)
and extract it into the root of the microSD card. It creates the ready-to-use
`/fonts/<family>/*.cpfont` tree.

For a single family, download the `.cpfont` files for the family you want from
the [CPR-vCodex SD font release](https://github.com/franssjz/cpr-vcodex/releases/tag/sd-fonts-m1-b4),
then create a folder with the family name and copy its `.cpfont` files to one
of two locations on your SD card:

- `/.fonts/` - hidden directory (preferred; keeps the SD root tidy when mounted on a desktop)
- `/fonts/` - visible directory (use this if your OS hides dot-files and you'd rather see the folder in your file manager)

Both roots are always scanned at boot and the results are merged: a family
installed in `/fonts/` shows up even when `/.fonts/` also exists, and vice
versa. The two roots only collide if the same family name appears in both. In
that case the copy in `/.fonts/` wins and the duplicate in `/fonts/` is ignored.

```text
SD Card Root/
|-- .fonts/                     Hidden root (preferred)
|   `-- ChareInk/
|       |-- ChareInk_12.cpfont
|       |-- ChareInk_14.cpfont
|       |-- ChareInk_16.cpfont
|       `-- ChareInk_18.cpfont
`-- fonts/                      Visible root (equally valid)
    |-- Lexend/
    |   |-- Lexend_10.cpfont
    |   |-- Lexend_12.cpfont
    |   |-- Lexend_14.cpfont
    |   |-- Lexend_16.cpfont
    |   `-- Lexend_18.cpfont
    `-- MyFont/
        |-- MyFont_12.cpfont
        `-- ...
```

Insert the SD card and power on your CPR-vCodex reader. The installed families
will appear under **Settings > Reader > Font Family**.

### Direct TTF/OTF/TTC fonts

If CrossPoint enables external RAM on your device, copy a `.ttf`, `.otf`, or
`.ttc` file directly into `/fonts/` or `/.fonts/`. The filename without its
extension becomes the family name. For example, `/fonts/Bookerly.ttf` appears as
`Bookerly` in **Settings > Reader > Font Family**.

For a family with separate styles, put its files in one subfolder. The folder
name becomes the family name. For example:

    /fonts/Bookerly/Bookerly-Regular.ttf
    /fonts/Bookerly/Bookerly-Bold.ttf
    /fonts/Bookerly/Bookerly-Italic.ttf
    /fonts/Bookerly/Bookerly-BoldItalic.ttf

The reader selects regular, bold, italic, and bold italic faces from the font
files. One regular file is enough; the reader derives missing styles. A `.cpfont`
file in the same family folder takes priority over TTF/OTF/TTC files, so keep
the two formats in separate folders. If both font roots contain the same family
name, the copy in `/.fonts/` takes priority.

Direct fonts use the standard reader sizes: 12, 14, 16, and 18 pt. The **Fonts**
tab in File Transfer accepts `.cpfont` files only. Copy direct fonts to the SD
card instead. To remove a loose font file, delete that file from the SD card.
The Fonts page can delete families stored in subfolders.

## Non-Latin Scripts Fallback in the User Interface

The built-in UI fonts do not cover every script. Book titles, file names, list
rows and headers may show replacement boxes for scripts outside their coverage,
even when book *content* renders correctly with a selected SD-card font.
CPR-vCodex retains its existing expanded built-in glyph coverage.

To avoid shipping every script's glyph set in flash, CrossPoint instead reuses
the SD-card font you already selected: when a UI string contains a character
the built-in font cannot draw, that whole string is rendered with your selected
SD-card font instead.

The fallback is **size-matched**. The built-in UI fonts render at 8 pt
(small/author lines), 10 pt (list rows) and 12 pt (book-cover titles, headers),
so CrossPoint loads your SD family at those sizes too and maps each UI font to
its same-size SD font. Non-Latin book names therefore appear at the same size
as the Latin text around them. A `.cpfont` family must contain files at sizes
**8, 10 and 12** for this UI fallback (in addition to the reader sizes 12–18) —
**missing these is the most common reason non-Latin UI text still shows
replacement boxes even though the book itself renders fine.** Any missing UI
size keeps showing boxes for that script at that size. Direct TTF/OTF/TTC
families use the same file at all three UI sizes.

These UI sizes only take effect if the reader-size font also matches one of
CrossPoint's built-in fallback-detection codepoints — see `kFallbackProbes` in
`src/SdCardFontSystem.cpp` for the current list. A script outside that list
gets no UI fallback even with the correct `.cpfont` sizes present.

For `.cpfont` families, **Settings > Reader > Font Size** lists every size the family ships,
so a family built at 8,10,12,14,16,18 offers all six as reading sizes — the UI
sizes are not hidden from the list. Reading at 8 pt is your call; if you would
rather not see the small sizes there, convert two families (one with the UI
sizes for fallback, one with only the reading sizes you want).

When converting your own font, include the UI sizes:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyCJKFont-Regular.otf \
      --intervals cjk \
      --sizes 8,10,12,14,16,18 \
      --style regular \
      --name MyCJKFont \
      --output-dir ./MyCJKFont/

What this means in practice:

- Select a non-Latin-capable SD font under **Settings > Reader > Font Family**
  (see [Installing Fonts](#installing-fonts) and the interval presets for
  `.cpfont` under [Converting Custom Fonts](#converting-custom-fonts)). That
  single selection drives both book content *and* size-matched fallback in
  the UI.
- Pure-Latin UI strings keep the crisp built-in font; only strings that
  actually contain a character the built-in font lacks are routed to the SD
  font.
- The fallback is per *string*, not per glyph: a mixed title such as
  `三体 Vol.1` renders entirely in the SD font (including the Latin part). If
  that SD font is a `Mono` family, the Latin portion will appear half/full
  width.
- If no SD font is selected (a built-in reading font is active), there is no
  fallback and the UI again shows boxes for non-Latin text — pick an SD font
  covering that script to restore it.

## Available Pre-Built Fonts

The current list of CPR-vCodex-only pre-built fonts is maintained in
`lib/EpdFont/scripts/sd-fonts.yaml` and published as CPR-vCodex release assets:

- Stable device manifest: https://github.com/franssjz/cpr-vcodex/releases/tag/sd-fonts-m1-b4
- Manual CPR-vCodex font package: https://github.com/franssjz/cpr-vcodex/releases/download/sd-fonts-m1-b4/all-fonts.zip
- Device manifest: https://github.com/franssjz/cpr-vcodex/releases/download/sd-fonts-m1-b4/fonts.json

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

> **Using this font for non-Latin UI fallback too?** The examples above only
> include the reader sizes (12–18). To also get fallback for this font's
> script in the library, headers, and file browser (see
> [Non-Latin Scripts Fallback in the User Interface](#non-latin-scripts-fallback-in-the-user-interface)),
> add the UI sizes: `--sizes 8,10,12,14,16,18`.

### Available Unicode interval presets

| Preset | Coverage |
|--------|----------|
| `ascii` | U+0020–U+007E (Basic Latin) |
| `latin1` | U+0080–U+00FF (Latin-1 Supplement) |
| `latin-ext` | European languages (Latin + Extended-A/B + punctuation + ligatures) |
| `greek` | Greek + Extended Greek |
| `cyrillic` | Cyrillic + Supplement |
| `hebrew` | Hebrew + Alphabetic Presentation Forms |
| `arabic` | Arabic + Supplement + Extended-A + Presentation Forms A/B (RTL, contextual shaping) |
| `georgian` | Georgian + Georgian Supplement |
| `armenian` | Armenian |
| `ethiopic` | Ethiopic + Extended |
| `vietnamese` | Vietnamese subset (ơ/ư and combining marks) |
| `ipa-chars` | IPA Extensions + Spacing Modifier Letters (phonetic transcription) |
| `punctuation` | General punctuation (U+2000–U+206F) |
| `cjk` | CJK Unified Ideographs + Hiragana + Katakana + Fullwidth |
| `hangul` | Korean Hangul syllables + Jamo + Compatibility Jamo |
| `cherokee` | Cherokee (historic + supplement block) |
| `tifinagh` | Tifinagh |
| `symbols` | Math, currency, arrows, box-drawing, misc symbols, dingbats |
| `reading` | Literary fiction coverage: Latin, Greek, Cyrillic, math/symbol blocks, supplemental punctuation, and CJK quote marks |
| `builtin` | Matches the firmware's built-in font conversion intervals |

Combine presets with commas: `--intervals latin-ext,greek,cyrillic`

You can also specify arbitrary Unicode ranges directly:
`--intervals latin-ext,(0x2100-0x214F)`

To list all presets with codepoint counts:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py --list-presets

### Additional options

`--force-autohint` - force FreeType's auto-hinter instead of the font's native hinting (useful when a font's built-in hints produce poor results at small sizes).

Install custom fonts via the web interface upload or manual SD card copy.
