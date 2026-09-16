"""Render regression for #215 using a generated EPUB (no copyrighted fixtures).

Run on Linux/WSL with the X4 simulator built as described in
agent-docs/simulator.md:
  python3 test/simulator/epub_image_grayscale.py /path/to/program

Each case gets a new temporary SD. A second boot reuses the image cache.
The simulator validates composition, not physical e-ink waveform quality.
"""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib


def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def make_epub(path, with_image):
    # Four gray bands and black diagram lines: losing either gray plane must
    # visibly alter the rendered image, even when the cached pixels are valid.
    rows = bytearray()
    for y in range(360):
        rows.append(0)  # PNG filter: none
        for x in range(360):
            rows.append(0 if x % 90 < 2 or y % 90 < 2 else (42, 85, 170, 220)[x // 90])
    png = (b'\x89PNG\r\n\x1a\n' +
           png_chunk(b'IHDR', struct.pack('>IIBBBBB', 360, 360, 8, 0, 0, 0, 0)) +
           png_chunk(b'IDAT', zlib.compress(rows)) + png_chunk(b'IEND', b''))
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as book:
        book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        book.writestr('META-INF/container.xml',
                      '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">'
                      '<rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/>'
                      '</rootfiles></container>')
        book.writestr('book.opf',
                      '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="uid">'
                      '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:identifier id="uid">image-gray-regression</dc:identifier>'
                      '<dc:title>Image grayscale regression</dc:title><dc:language>en</dc:language></metadata>'
                      '<manifest><item id="ch" href="chapter.xhtml" media-type="application/xhtml+xml"/>'
                      '<item id="img" href="diagram.png" media-type="image/png"/></manifest>'
                      '<spine><itemref idref="ch"/></spine></package>')
        image = '<img src="diagram.png"/>' if with_image else ''
        book.writestr('chapter.xhtml',
                      '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Test</title></head>'
                      '<body><p>Readable text stays crisp.</p>' + image + '</body></html>')
        book.writestr('diagram.png', png)


def read_bmp(path):
    data = path.read_bytes()
    assert data[:2] == b'BM', path
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    assert bits in (24, 32), bits
    stride = ((width * bits + 31) // 32) * 4
    rows = []
    for y in range(abs(height)):
        source_y = height - 1 - y if height > 0 else y
        start = offset + source_y * stride
        rows.append([tuple(data[start + x * (bits // 8):start + x * (bits // 8) + 3])
                     for x in range(width)])
    return rows


def crop(rows, left, top, right, bottom):
    return [pixel for row in rows[top:bottom] for pixel in row[left:right]]


def gray_count(pixels):
    return sum(0 < r < 255 and r == g == b for b, g, r in pixels)


def run_case(program, root, aa, dark, with_image):
    sd = root / f'image{int(with_image)}-aa{aa}-dark{dark}'
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    make_epub(sd / 'test.epub', with_image)
    (store / 'settings.json').write_text(json.dumps({
        'language': 'EN', 'textAntiAliasing': aa, 'darkMode': dark,
        'orientation': 0, 'fontFamily': 0, 'fontFamilySchemaVersion': 3,
        'fontSize': 18, 'fontSizeSchemaVersion': 3, 'screenMargin': 5,
        'readerRefreshMode': 0, 'imageRendering': 0,
    }))
    captures = []
    for boot in ('decode', 'cached'):
        (store / 'state.json').write_text(json.dumps({
            'openEpubPath': '/test.epub', 'lastSleepFromReader': True,
            'readerActivityLoadCount': 0, 'showBootScreen': False,
        }))
        screenshot = sd / f'{boot}.bmp'
        env = os.environ.copy()
        env.update(CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT='8500:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=f'7500:{screenshot}')
        with (sd / f'{boot}.log').open('w') as log:
            subprocess.run([str(program)], env=env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=30, check=True)
        rows = read_bmp(screenshot)
        assert len(rows) == 800 and len(rows[0]) == 480, 'Use the X4 portrait simulator'
        captures.append(rows)
        # The paragraph is above the image. UI/status bar pixels are excluded.
        text = crop(rows, 10, 15, 470, 45)
        if aa and not dark:
            assert gray_count(text) > 0, f'{sd.name}/{boot}: text AA disappeared'
        else:
            assert gray_count(text) == 0, f'{sd.name}/{boot}: unwanted text AA'
        picture = crop(rows, 100, 180, 380, 380)
        if with_image:
            assert gray_count(picture) > 10000, f'{sd.name}/{boot}: image gray planes missing'
        else:
            assert gray_count(picture) == 0, f'{sd.name}/{boot}: unexpected gray on empty page'
    # Content must not change when switching from decoder output to cache.
    assert crop(captures[0], 0, 0, 480, 750) == crop(captures[1], 0, 0, 480, 750), sd.name
    print(f'PASS {sd.name}: decode and cached', flush=True)
    return crop(captures[0], 100, 180, 380, 380)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='image-gray-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)
    pictures = {}
    for dark in (0, 1):
        for aa in (0, 1):
            pictures[dark, aa] = run_case(args.program.resolve(), root, aa, dark, True)
        assert pictures[dark, 0] == pictures[dark, 1], 'Text AA changed image rendering'
    run_case(args.program.resolve(), root, 0, 0, False)
    (root / 'result.json').write_text(json.dumps({'status': 'passed', 'boots': 10}))
    print('PASS: image tones independent of text AA; text preference and caches preserved', flush=True)


if __name__ == '__main__':
    main()
