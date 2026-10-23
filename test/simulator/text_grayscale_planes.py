"""Compare dual text strips with the low-memory two-pass fallback for #229.

Run with the X4 simulator from agent-docs/simulator.md. Heap reporting selects
the paths; this does not model allocation failures or physical panel timing.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

from epub_image_grayscale import gray_count, make_epub, read_bmp


def make_text_book(path):
    make_epub(path, False)
    with zipfile.ZipFile(path) as original:
        entries = {name: original.read(name) for name in original.namelist()}
    paragraph = (
        '<p>Readable <b>bold</b> and <i>italic</i> text with '
        '<u>underlined edges</u>, <s>struck words</s>, '
        'H<sub>2</sub>O and x<sup>2</sup>. '
        'The quick brown fox jumps over the lazy dog.</p>'
    )
    entries['chapter.xhtml'] = (
        '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Text planes</title></head>'
        '<body><h2>Grayscale strip comparison</h2><hr/>' + paragraph * 6 + '</body></html>'
    ).encode()
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as book:
        for name, data in entries.items():
            book.writestr(name, data)


def run_case(program, root, orientation, dual, highlight=False, fragmented=False):
    suffix = '-fragmented' if fragmented else '-highlight' if highlight else ''
    sd = root / f'orientation{orientation}-dual{int(dual)}{suffix}'
    if not dual or highlight:
        # Build pagination with normal memory first. Otherwise the parser's
        # independent low-heap guard aborts before grayscale is reached.
        source_suffix = '-highlight' if highlight and not dual else ''
        shutil.copytree(root / f'orientation{orientation}-dual1{source_suffix}', sd)
    store = sd / '.crosspoint'
    store.mkdir(parents=True, exist_ok=True)
    if dual and not highlight:
        make_text_book(sd / 'test.epub')
    if dual and highlight:
        # Version 5 BookmarkStore record: highlight the three heading words.
        book_dirs = list((store / 'bookdata').iterdir())
        assert len(book_dirs) == 1, book_dirs
        snippet = b'Grayscale strip comparison'
        record = struct.pack('<BIHHBHHHH', 5, 1, 0, 0, 1, 0, 0, 2, len(snippet))
        (book_dirs[0] / 'bookmarks.bin').write_bytes(record + snippet + struct.pack('<BI', 0, 0))
    (store / 'settings.json').write_text(json.dumps({
        'language': 'EN', 'textAntiAliasing': 1, 'darkMode': 0,
        'orientation': orientation, 'textDarkness': orientation, 'bionicReading': orientation % 3,
        'fontFamily': 0, 'fontFamilySchemaVersion': 3, 'fontSize': 18, 'fontSizeSchemaVersion': 3,
        'screenMargin': 5, 'readerRefreshMode': 0,
        'statusBarChapterPageCount': 0, 'statusBarBookProgressPercentage': 0,
        'statusBarProgressBar': 2, 'statusBarTitle': 2, 'statusBarBattery': 0, 'statusBarClock': 0,
    }))
    (store / 'state.json').write_text(json.dumps({
        'openEpubPath': '/test.epub', 'lastSleepFromReader': True,
        'readerActivityLoadCount': 0, 'showBootScreen': False,
    }))
    captures = [sd / f'page{i}.bmp' for i in range(3)]
    env = os.environ.copy()
    env.update(
        SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
        CROSSPOINT_SIM_FREE_HEAP=str(200000 if dual or fragmented else 20000),
        CROSSPOINT_SIM_MAX_ALLOC_HEAP=str(20000 if fragmented else 100000),
        CROSSPOINT_SIM_INPUT_SCRIPT='6000:RIGHT;8000:LEFT;10300:QUIT',
        CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{t}:{p}' for t, p in zip((5500, 7500, 9500), captures)),
    )
    log_path = sd / 'run.log'
    with log_path.open('w') as log:
        subprocess.run([str(program)], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=30, check=True)
    log = log_path.read_text()
    timings = [line for line in log.splitlines() if 'Page render (tiled):' in line]
    assert len(timings) >= 3, f'{sd}: missing page turns: {timings}'
    assert all(f'dual={int(dual)} ' in line for line in timings), f'{sd}: wrong rendering path: {timings}'
    images = [read_bmp(path) for path in captures]
    assert images[0] != images[1], f'{sd}: next page did not change'
    assert images[0] == images[2], f'{sd}: returning to page changed its pixels'
    assert all(gray_count(pixel for row in image for pixel in row) > 100 for image in images), f'{sd}: missing AA'
    return images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='text-planes-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)
    baseline = None
    for orientation in range(4):
        dual = run_case(args.program.resolve(), root, orientation, True)
        fallback = run_case(args.program.resolve(), root, orientation, False)
        assert dual == fallback, f'orientation {orientation}: dual and fallback pixels differ'
        if orientation == 0:
            baseline = dual
        print(f'PASS orientation {orientation}: dual/fallback and forward/back pixels match', flush=True)
    highlighted = run_case(args.program.resolve(), root, 0, True, highlight=True)
    fallback = run_case(args.program.resolve(), root, 0, False, highlight=True)
    assert highlighted == fallback, 'Highlight dual/fallback pixels differ'
    assert highlighted[0] != baseline[0], 'Highlight fixture did not paint'
    print('PASS highlight: dithered background and overlaid glyphs match', flush=True)
    fragmented = run_case(args.program.resolve(), root, 0, False, fragmented=True)
    assert fragmented == baseline, 'Fragmented-heap fallback pixels differ'
    print('PASS fragmented heap: fallback preserves pixels with high total free heap', flush=True)
    (root / 'result.json').write_text(json.dumps({'status': 'passed', 'boots': 11, 'page_captures': 33}))


if __name__ == '__main__':
    main()
