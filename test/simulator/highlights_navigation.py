"""Exercise issue #227 on a normal X4 simulator with real persisted highlights.

Generate a Korean SD font with the existing converter (8,10,12,18 point sizes),
then pass its family directory with --font-dir. No firmware instrumentation is
needed. Disposable SDs, screenshots, syscall traces, and logs are retained.
Optional strace counters measure host file opens, not physical SD latency.
The dense case deliberately exceeds the fallback cache's 512-glyph union.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from epub_image_grayscale import crop, make_epub, read_bmp


def bookmarks_bytes(count, length):
    records = bytearray(struct.pack('<BI', 5, count))
    for index in range(count):
        # Different syllables per row make later windows need different glyphs.
        syllables = ''.join(chr(0xAC00 + (index * 37 + i * 13) % 11172)
                            for i in range(96 if length == 'dense' else 12))
        snippet = f'{index + 1:03d} 책을 읽으며 기억하고 싶은 문장입니다. {syllables}'
        if length == 'long':
            snippet += ' 오늘 읽은 이야기는 오래 기억에 남습니다. 새로운 생각을 만나고 세상을 바라보는 마음을 넓힙니다.' * 3
        raw = snippet.encode('utf-8')
        records += struct.pack('<HHBHHHH', 0, index, 1, index, 0, 10, len(raw))
        records += raw + struct.pack('<BI', 0, 0)
    return records


def boot(program, sd, name, events, captures=(), trace=False):
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{t}:{key}' for t, key in events),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{t}:{sd / (label + ".bmp")}'
                                                  for t, label in captures))
    command = [str(program)]
    if trace:
        command = ['strace', '-f', '-ttt', '-e', 'trace=openat', '-o',
                   str(sd / f'{name}.strace')] + command
    with (sd / f'{name}.log').open('w') as log:
        subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=events[-1][0] / 1000 + 30, check=True)
    return (sd / f'{name}.log').read_text(errors='replace')


def screenshot_content(path):
    rows = read_bmp(path)
    assert len(rows) == 800 and len(rows[0]) == 480, 'Use a portrait X4 simulator'
    pixels = crop(rows, 0, 75, 480, 735)
    dark = sum(max(p) < 128 for p in pixels)
    assert dark > 1500, f'{path}: missing list text'
    return hashlib.sha256(bytes(channel for p in pixels for channel in p)).hexdigest()


def run_case(program, root, count, length, font_dir, theme, trace):
    name = f'{length}-{count}-theme{theme}'
    sd = root / name
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    make_epub(sd / 'test.epub', False)
    settings = {
        'language': 'EN', 'uiTheme': theme, 'uiThemeSchemaVersion': 3,
        'readerMenuStyle': 0, 'fontFamily': 0, 'fontFamilySchemaVersion': 3,
        'fontSize': 18, 'fontSizeSchemaVersion': 3, 'orientation': 0,
        'textAntiAliasing': 0, 'displayDay': 0, 'refreshFrequency': 5,
    }
    if font_dir:
        shutil.copytree(font_dir, sd / 'fonts' / font_dir.name)
        settings['sdFontFamilyName'] = font_dir.name
    (store / 'settings.json').write_text(json.dumps(settings))
    state = {'openEpubPath': '/test.epub', 'lastSleepFromReader': True,
             'readerActivityLoadCount': 0, 'showBootScreen': False}

    def reset_state():
        (store / 'state.json').write_text(json.dumps(state))

    reset_state()
    setup = boot(program, sd, 'setup', [(6500, 'QUIT')])
    assert 'Entering activity: EpubReader' in setup, f'{name}: reader did not open'
    if font_dir:
        assert f'Loaded SD font family: {font_dir.name}' in setup, \
            f'{name}: Korean font not loaded'
        assert f'{font_dir.name}_10.cpfont' in setup, f'{name}: UI fallback not loaded'
    # The reader establishes the content-derived identity; seed its real store.
    data_dirs = list((store / 'bookdata').iterdir())
    assert len(data_dirs) == 1, f'{name}: unexpected book identities'
    marks = data_dirs[0] / 'bookmarks.bin'
    marks.write_bytes(bookmarks_bytes(count, length))
    reset_state()
    events = [(5500, 'ENTER')] + [(6500 + i * 1000, 'DOWN') for i in range(5)]
    events += [
        (11500, 'ENTER'), (14500, 'UP'), (17500, 'DOWN'),
        (20500, 'DOWN'), (23500, 'UP'), (26500, 'UP'),
        (29500, 'ENTER'), (33000, 'QUIT'),
    ]
    captures = [(13500, 'first'), (16500, 'last'), (19500, 'first-return'),
                (22500, 'second'), (25500, 'first-again'), (28500, 'last-again')]
    log = boot(program, sd, 'navigation', events, captures, trace)
    digests = {label: screenshot_content(sd / f'{label}.bmp') for _, label in captures}
    report = {'case': name, 'highlights': count, 'traced': trace,
              'font': font_dir.name if font_dir else None,
              'screenshots': digests}
    renders = [(int(t), int(duration)) for t, duration in
               re.findall(r'\[(\d+)\].*?\[GFX\] Time = (\d+) ms', log)]
    list_renders = [(t, duration) for t, duration in renders if 11500 < t < 29500]
    report['list_renders_ms'] = [duration for _, duration in list_renders]
    report['first_wrap_latency_ms'] = next(
        (t - 14500 for t, _ in list_renders if t >= 14500), None)
    if trace:
        lines = (sd / 'navigation.strace').read_text().splitlines()
        report['ui_font_opens'] = sum('_10.cpfont' in line and 'O_RDONLY' in line for line in lines)
        report['sd_font_opens'] = sum('.cpfont' in line and 'O_RDONLY' in line for line in lines)
    # The one-page fixture clamps out-of-range targets and logs the exact
    # selected bookmark page first, checking logical selection as well as pixels.
    checks = [
        ('Entering activity: Bookmarks' in log, 'highlights did not open'),
        (digests['first'] == digests['first-return'] == digests['first-again'],
         'wrapping did not restore the first viewport'),
        (digests['last'] == digests['last-again'],
         'repeated backward wrap changed the last viewport'),
        (digests['first'] != digests['last'], 'UP did not reach another viewport'),
        (digests['first'] != digests['second'], 'DOWN did not move selection'),
        (f'Clamping cached page {count - 1} to 0' in log,
         'activation after backward wrap did not target the final highlight'),
    ]
    report['failures'] = [message for condition, message in checks if not condition]
    report['status'] = 'failed' if report['failures'] else 'passed'
    (sd / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)
    assert not report['failures'], f'{name}: ' + '; '.join(report['failures'])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--font-dir', type=Path)
    parser.add_argument('--count', type=int, default=256)
    parser.add_argument('--length', choices=('short', 'long', 'dense', 'both', 'all'), default='all')
    parser.add_argument('--theme', type=int, choices=(0, 1, 3), default=1)
    parser.add_argument('--strace', action='store_true')
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    args = parser.parse_args()
    assert 2 <= args.count <= 256
    if args.font_dir:
        args.font_dir = args.font_dir.resolve()
        for size in (10, 12, 18):
            assert (args.font_dir / f'{args.font_dir.name}_{size}.cpfont').is_file()
    root = Path(tempfile.mkdtemp(prefix='highlights-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)
    results = [run_case(args.program.resolve(), root, args.count, length,
                        args.font_dir, args.theme, args.strace)
               for length in ('short', 'long', 'dense')
               if args.length == length or args.length == 'all' or
               (args.length == 'both' and length != 'dense')]
    (root / 'result.json').write_text(json.dumps(results, indent=2))
    print('PASS: real Highlights wrapping, selection, text pixels, and activation', flush=True)


if __name__ == '__main__':
    main()
