# Scripts

Utility scripts should be run from the repository root on Windows unless a
script says otherwise.

## EPUB load triage

Use these scripts before testing a suspicious EPUB on the device. They are
intended to catch files that can stall or fail during first open, cache build,
or first section rendering.

### Inspect an EPUB

```powershell
python -X utf8 scripts\inspect_epub.py "D:\path\to\book.epub"
```

For reader-startup problems, use the deeper spine scan:

```powershell
python -X utf8 scripts\inspect_epub.py "D:\path\to\book.epub" --spine --deep
```

What to look for:

- `XML parse failed in spine ...`: the chapter HTML is not well-formed XHTML.
  The firmware chapter parser uses Expat, so this is a likely on-device failure.
- `CSS over firmware parse limit`: embedded CSS is too large for the guarded CSS
  parsing path and will be skipped.
- Missing manifest, spine, TOC, or image references: the EPUB is structurally
  inconsistent and should be repaired or re-exported.
- Very large XHTML, image, or ZIP entry counts: not always fatal, but useful
  clues when load time or memory use looks suspicious.

### Repair common XHTML void-tag issues

Some optimized EPUBs contain browser-tolerated HTML such as:

```html
<meta charset="utf-8">
```

In XHTML spine files this must be self-closed:

```html
<meta charset="utf-8" />
```

Repair a copy of the EPUB with:

```powershell
python -X utf8 scripts\repair_epub_xhtml.py "D:\path\to\bad.epub" "D:\path\to\fixed.epub"
```

Then verify the fixed copy:

```powershell
python -X utf8 scripts\inspect_epub.py "D:\path\to\fixed.epub" --spine --deep
```

If the deep scan reports no obvious first-load risk factors, copy the fixed EPUB
to the device. If the original book was already opened on the device, clear its
`.crosspoint` cache entry before retesting.
