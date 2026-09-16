# Issue #215 — EPUB images lose gray detail

## Reproduction and cause (2026-09-16)

Report: <https://github.com/franssjz/cpr-vcodex/issues/215>.
The supplied `Books.zip` contains two EPUBs. Its SHA-256 is
`6fe493fd6ab6a3a6dbdd4ab3d85edd3fbed50db2859529d7e56ce4d123a03d80`.
Keep the books and derived samples under gitignored `artifacts/issue215/`;
do not distribute them as test fixtures.

The second comparison photograph shows `images/76.jpg` from the second book,
referenced by `ngoinhakyquai2/text-16.xhtml`. A local single-image EPUB using
that unmodified JPEG reproduces the distinctive failure: both floor plans
become black, leaving the diagonal white lines and the captions visible.

The trigger is light-mode reading with **text anti-aliasing disabled**.
`EpubReaderActivity::renderContents()` previously enabled image-only grayscale
only in dark mode. In light mode it skipped both gray planes when text AA
was off. `DirectPixelWriter` deliberately paints levels 0, 1 and 2 black in
the BW base, expecting the subsequent gray passes to restore levels 1 and 2.
Without those passes, correctly decoded gray backgrounds conceal their labels.
The simulator logs `grayscale=off` for the failing case.

This condition is present in tag `1.5.0.30-cpr-vcodex` and the current source;
it predates the reported .32/.33 attempts. The proportional dithering change
and cache invalidation did not address the missing display passes. Mathematical
tests of the dithering function alone could not catch this integration failure.

## Correction

Adapt Crosspoint commit `d1abcc00a2bcb83bfd7c03c6e995abaee7cd2615`
(#2393, “render grayscale epub images without text aa”). It is already in the
fork's ancestry, but the fork's reader retained the old condition. CrossInk's
reader likewise separates `needsImageGrayscale` from `needsTextGrayscale`.

- Render image-only gray passes whenever text AA is off, in either theme.
- Give light-mode image pages the same base refresh regardless of text AA.
- Keep the existing dark-mode base refresh, image polarity, refresh overrides,
  text preference, allocation checks and strip/fallback implementation.
- Keep the decoder, tone curve and PXC format unchanged. Existing caches have
  the required gray values and can be reused without deletion or migration.

## Validation

- **Negative control:** `test/simulator/epub_image_grayscale.py` fails on the
  pre-fix simulator with `image gray planes missing` in light mode, AA off.
- **Ten corrected simulator boots:** generated PNG diagram with text AA on/off,
  light/dark themes, initial decode and a second boot using the cache; plus a
  text-only page. Check rendered screenshot pixels, image equality across AA
  settings, unchanged cached output, and monochrome text when AA is disabled.
  Results: `artifacts/issue215/image-gray-viht9auk/result.json`.
- **Reported JPEG:** fresh decode and reuse of the pre-fix PXC cache with AA
  disabled both produce the same image pixels as the pre-fix AA-enabled
  reference (480 x 778 content crop). The reused PXC is byte-for-byte unchanged.
  Results: `artifacts/issue215/jpeg-results.json`; screenshots in
  `before-aa0`, `before-aa1`, `after-cached-aa0`, `after-fresh-aa0`.
- **Firmware:** `platformio run -e default -j 1` passes for ESP32-C3.
  Development BIN `1.6.0.38.dev2-fddda85c-cpr-vcodex.bin`: 6,166,912 bytes,
  386,688 bytes below the X4 app slot. This is a new development candidate;
  the earlier normal .38 OTA candidate does not include this image fix.
- **Existing native suite:** 269/269 tests pass. `git diff --check` and Python
  syntax compilation pass. Logs: `artifacts/issue215/default-build.log` and
  `native-tests.log`.

The simulator validates the missing gray-plane composition, not physical
e-ink tones, refresh residue or ESP32 memory pressure. Physical X4/X3 image
confirmation remains pending. The issue has not been closed or marked
hardware-verified. The subsequent combined development candidate
`1.6.0.38.dev3-fddda85c` (also containing JPEG #2925) was installed on the
maintainer's X4: its flash digest, two boots and the confirmed OTA selector
were verified. The reported EPUBs have not yet been visually checked on that
device. See the stability audit for the installation and release record.
