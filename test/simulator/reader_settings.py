"""Reader settings/marks regressions on the normal X4 simulator, no firmware patch.

Uses generated EPUBs and disposable SDs. Asserts persisted state and preview
pixels, and retains screenshots for visual review. Run on Linux/WSL.
"""

import argparse
import fnmatch
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from epub_image_grayscale import crop, make_epub, read_bmp


def run_case(program, root, name, presses, initial=None, expected=None, marks=None,
             activity=None, theme=1, language='EN', broken_font=False, interval=950):
    assert all(key in {'ENTER', 'BACK', 'LEFT', 'RIGHT', 'UP', 'DOWN'} for key in presses), \
        f'{name}: unsupported simulator key'
    sd = root / name
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    make_epub(sd / 'test.epub', False)
    if broken_font:
        fonts = sd / 'fonts' / 'Broken'
        fonts.mkdir(parents=True)
        (fonts / 'Broken_19.cpfont').write_bytes(b'Invalid font for load-failure regression')
    settings = {
        'language': language, 'uiTheme': theme, 'uiThemeSchemaVersion': 3,
        'readerMenuStyle': 0, 'fontFamily': 0, 'fontFamilySchemaVersion': 3,
        'fontSize': 18, 'fontSizeSchemaVersion': 3, 'bionicReading': 2,
        'orientation': 0, 'textAntiAliasing': 0, 'displayDay': 0,
        'refreshFrequency': 5,
    }
    settings.update(initial or {})
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({
        'openEpubPath': '/test.epub', 'lastSleepFromReader': True,
        'readerActivityLoadCount': 0, 'showBootScreen': False,
    }))
    # Leave room for e-ink refreshes and first-time pagination on slow hosts.
    events = [(5500 + i * interval, key) for i, key in enumerate(presses)]
    end = 5500 + len(presses) * interval
    events.append((end + 2300, 'QUIT'))
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{t}:{key}' for t, key in events),
               CROSSPOINT_SIM_SCREENSHOTS=f'{end + 1400}:{sd / "result.bmp"}')
    with (sd / 'run.log').open('w') as output:
        subprocess.run([str(program)], env=env, stdout=output, stderr=subprocess.STDOUT,
                       timeout=(end / 1000) + 20, check=True)
    log = (sd / 'run.log').read_text(errors='replace')
    if activity:
        assert f'Entering activity: {activity}' in log, f'{name}: wrong destination'
    if broken_font:
        assert 'Found family: Broken' in log, f'{name}: reader picker did not discover SD fonts'
        assert 'Failed to load SD font family: Broken' in log, f'{name}: wrong font selected'
    saved = json.loads((store / 'settings.json').read_text())
    for key, value in (expected or {}).items():
        assert saved.get(key) == value, f'{name}: {key}={saved.get(key)}, expected {value}'
    if marks is not None:
        files = list(store.rglob('bookmarks.bin'))
        assert len(files) == 1, f'{name}: missing bookmark store'
        count = struct.unpack_from('<I', files[0].read_bytes(), 1)[0]
        assert count == marks, f'{name}: {count} marks, expected {marks}'
    pixels = read_bmp(sd / 'result.bmp')
    assert any(pixel != (255, 255, 255) for pixel in crop(pixels, 0, 0, 480, 750)), name
    if activity == 'TextSettings' or name == 'bionic-cancel':
        assert any(pixel != (255, 255, 255) for pixel in crop(pixels, 0, 0, 480, 85)), \
            f'{name}: missing editor header'
        assert any(pixel != (255, 255, 255) for pixel in crop(pixels, 25, 100, 455, 210)), \
            f'{name}: blank text preview'
    print(f'PASS {name}', flush=True)
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--only', action='append', help='Case name glob; repeatable')
    parser.add_argument('--theme', type=int, default=1)
    parser.add_argument('--language', default='EN')
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='reader-settings-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)

    def run(name, presses, **kwargs):
        if args.only and not any(fnmatch.fnmatchcase(name, pattern) for pattern in args.only):
            return None
        return run_case(args.program.resolve(), root, name, presses,
                        theme=args.theme, language=args.language, **kwargs)

    list_save = ['ENTER'] + ['DOWN'] * 6 + ['ENTER']
    run('marks-list', list_save * 2, marks=1, activity='EpubReaderMenu')
    toolbar_save = ['ENTER', 'RIGHT', 'RIGHT', 'ENTER'] + ['DOWN'] * 5 + ['ENTER']
    run('marks-toolbar', toolbar_save * 2, initial={'readerMenuStyle': 1}, marks=1)

    for value in range(6):
        run(f'refresh-{value}', ['ENTER', 'ENTER', 'DOWN', 'ENTER'],
            initial={'refreshFrequency': value}, expected={'refreshFrequency': (value + 1) % 6},
            activity='ReaderQuickSettings')

    text = ['ENTER', 'UP', 'ENTER', 'ENTER', 'ENTER', 'ENTER', 'DOWN', 'ENTER']
    toolbar = ['ENTER', 'RIGHT', 'ENTER'] + ['DOWN'] * 4 + ['ENTER']
    subtle_editor = None
    for mode, move in ((0, ['DOWN']), (1, ['UP']), (2, [])):
        edited = run(f'bionic-text-{mode}', text + move + ['ENTER'],
                     expected={'bionicReading': mode}, activity='TextSettings')
        if mode == 2:
            subtle_editor = edited
        run(f'bionic-toolbar-{mode}', toolbar + move + ['ENTER'],
            initial={'readerMenuStyle': 1}, expected={'bionicReading': mode})
        run(f'bionic-quick-{mode}', ['ENTER', 'ENTER'] + ['DOWN'] * 10 + ['ENTER'],
            initial={'bionicReading': (mode + 2) % 3}, expected={'bionicReading': mode},
            activity='ReaderQuickSettings')

    # Selecting/cancelling a picker must not reinterpret an existing Subtle value.
    cancelled = run('bionic-cancel', text + ['UP', 'BACK'], expected={'bionicReading': 2})
    if cancelled and subtle_editor:
        assert crop(cancelled, 0, 298, 480, 710) == crop(subtle_editor, 0, 298, 480, 710), \
            'Cancel did not close the picker and restore the unchanged settings'
    run('font-text', ['ENTER', 'UP', 'ENTER', 'DOWN', 'DOWN', 'ENTER'],
        expected={'fontFamily': 1, 'fontSize': 18}, activity='TextSettings')
    run('font-quick', ['ENTER', 'ENTER'] + ['DOWN'] * 3 + ['ENTER', 'DOWN', 'ENTER'],
        expected={'fontFamily': 1, 'fontSize': 18}, activity='FontSelect')
    failed_font = run('font-failure', ['ENTER', 'UP', 'ENTER'] + ['DOWN'] * 3 + ['ENTER'],
                      expected={'fontFamily': 0, 'sdFontFamilyName': None, 'fontSize': 18}, broken_font=True,
                      activity='TextSettings')

    previews = []
    for mode in range(3):
        pixels = run(f'preview-{mode}', ['ENTER', 'UP', 'ENTER'],
                     initial={'bionicReading': mode}, activity='TextSettings')
        if pixels:
            # Sample text only, excluding the label, tabs and settings rows.
            previews.append(crop(pixels, 25, 100, 455, 210))
            if mode == 2 and subtle_editor:
                # Classic's tab band starts a few pixels above Lyra's.
                assert crop(pixels, 0, 0, 480, 290) == crop(subtle_editor, 0, 0, 480, 290), \
                    'Switching tabs or closing a picker damaged the header or preview'
            if mode == 2 and failed_font:
                assert crop(pixels, 0, 100, 480, 290) == crop(failed_font, 0, 100, 480, 290), \
                    'Failed SD font still named in preview, or fallback size not restored'
    if len(previews) == 3:
        assert previews[0] != previews[1] != previews[2] != previews[0], 'Bionic preview modes collapsed'
    more = ['ENTER', 'RIGHT', 'RIGHT', 'ENTER'] + ['DOWN'] * 9
    auto_off = run('auto-initial', more + ['ENTER'], initial={'readerMenuStyle': 1}, interval=1800)
    auto_stopped = run('auto-stopped', more + ['ENTER', 'DOWN', 'ENTER', 'BACK', 'BACK', 'ENTER'] +
                       more + ['ENTER'], initial={'readerMenuStyle': 1}, interval=1800)
    if auto_off and auto_stopped:
        assert crop(auto_off, 0, 200, 480, 710) == crop(auto_stopped, 0, 200, 480, 710), \
            'Stopped auto-turn still displays the previous rate as active'
    print('PASS: reader settings and marks matrix', flush=True)


if __name__ == '__main__':
    main()
