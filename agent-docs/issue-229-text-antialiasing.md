# Text anti-aliasing page turns (#229)

Implementation: 2026-10-23. Hardware validation remains pending.

[Issue #229](https://github.com/franssjz/cpr-vcodex/issues/229) reports roughly
two-second X3 page turns with text AA enabled. The X3 still needs its synchronous
grayscale base, gray waveform and controller baseline cleanup. This change
reduces software work; it does not remove those display operations or promise
monochrome page-turn speed.

## Implementation

- Text-only overlay pages render both gray masks in one traversal per physical
  strip. Glyph clipping, rotation and packed-pixel decoding are shared.
- The optional second 80-row strip costs 8,000 bytes on X3/X4. It is allocated
  once per grayscale page pass, reserving at least 16 KiB of both free heap
  and contiguous allocation capacity. Insufficient heap, a small largest free
  block, or an allocation failure retains the original two-pass renderer.
- Existing asynchronous full-plane buffering also shares text traversal when
  both full-plane buffers are available. Its wait/upload ordering is retained.
- Monochrome primitives, scaled glyphs and dithered rectangles write both
  targets so text decorations and highlights retain their prior composition.
  Ending a strip clears both borrowed pointers; the BW framebuffer stays intact.
- Image pages, A4 status-bar passes, absolute grayscale encoding, dark mode,
  panel capabilities, waveforms and controller cleanup keep their existing
  paths. No new UI mode.

## Verification

- Final `default` and `gh_release` builds passed, as did their 97.5% flash
  budget checks. ELF flash usage: 96.95% / 95.74%; packaged BIN sizes:
  6,367,728 / 6,288,512 bytes. Static RAM: 62,088 bytes in both configurations.
  These builds include the checkout's pre-existing OPDS edits, left untouched.
  Release-counter updates were disabled; downloaded release artifacts were
  restored and hash-verified after preserving the candidate under
  `artifacts/issue-229/release-candidate/`.
- `GlyphBitmapTest`: four tests passed, including 30,720 dual-plane comparisons
  against independent LSB/MSB draws. Covers 1/2-bpp, all darkness values, ink
  polarity, panel/text rotations, clipping, partial strips and guarded buffers.
- `test/simulator/text_grayscale_planes.py`: eleven X4 simulator boots and 33
  captures passed. All four orientations compare dual rendering against the
  two-pass fallback, with forward/back page turns, darkness levels 0–3, bionic
  modes, bold/italic, underlines, strikethrough, horizontal rules and sub/sup text.
  Highlighted heading text also verifies dithered backgrounds and glyph redraws.
  A fragmented-heap case verifies fallback with high total free heap.
  Captures match pixel for pixel. The fallback reuses a completed pagination
  cache so the parser's separate low-heap guard does not prevent the test.
- `test/simulator/epub_image_grayscale.py`: all ten existing boots passed for
  AA on/off, dark/light, decoded/cached images and monochrome text.
- Simulator output checks composition only. Reported low heap selects the
  fallback; it does not simulate a failed native allocation or real C3 RAM.

Local logs/captures live in the ignored `artifacts/issue-229/` directory. The
isolated WSL workspace is recorded there in `simulator-workspace.txt`. Its two
crypto callers explicitly include the existing simulator crypto shim to avoid
the SDK header taking precedence; this adjustment is confined to that copy.

## Hardware validation still required

Compare release 1.6.5.6 and the candidate on one X3 and one X4, using the same
EPUB, font/size, orientation, darkness and refresh cadence. Include built-in
text and an SD/CJK font, AA on/off, and at least ten warm-cache turns per case.
Record median/min/max input-to-settle time, panel/controller revision and any
fading, speckles, ghosting, missing edges or bad subsequent BW refreshes.

Reader timing logs report base `display`, gray rendering/upload, `gray_display`,
`cleanup`, and `total`. The tiled path reports `wait_alloc` separately and labels
its first pass `gray_lsb_or_dual`; `dual=1` means both masks came from that pass.
The async path retains separate `gray_render`, `wait`, and `gray_write` fields.
Both report heap and largest allocatable block before grayscale allocations.
These totals start inside `renderContents`, not at the physical button press.

The issue's suggested 80–110 ms software saving is an estimate, not a measured
result from this implementation. Physical waveform quality and X3/X4 timings
remain release gates; no hardware was flashed or release published here.
