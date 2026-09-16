# JPEGDEC progressive scan regression

These tests compile the same JPEGDEC revision pinned by `platformio.ini`, with
all production patches from `scripts/jpegdec_patches/`. CMake fetches that
revision and patches an isolated build copy. Neither a device nor PlatformIO's
installed dependencies are required. The desktop simulator uses a different
decoder and cannot validate this regression.

`generate_fixtures.py` creates five original 64 x 64 JPEGs using only Python's
standard library. Each 8 x 8 tile is a known constant gray. The tests request
8-bit grayscale at 1/8 scale, as firmware does for progressive JPEGs, and check
every output pixel and callback bounds:

- Baseline color JPEG (control).
- Progressive color with interleaved DC components (control).
- Progressive color with separate Y, Cb and Cr DC scans (#2925).
- Progressive grayscale (control).
- Progressive color with different Cb and Cr DC Huffman tables (#2925).

All progressive files include explicit zero AC scans. The generated images
were independently decoded with Pillow/libjpeg; all full-resolution pixels
match the intended gray tiles. With only patches 0001/0002, the separate-scan
and distinct-Cr-table cases both return `JPEG_DECODE_ERROR`; after patch 0003,
all five cases pass. No external book images are used.

Run with the normal native suite:

```sh
cmake -S test -B build/test
cmake --build build/test -j2
cd build/test
ctest --output-on-failure -R '^jpegdec_'
```

The first configure needs Git/network access, just like the suite's existing
GoogleTest dependency. The test pin is checked against `platformio.ini` so a
firmware dependency upgrade cannot silently leave these tests on an old decoder.
