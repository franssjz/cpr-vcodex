"""Exercise real book/flashcard sleep and wake routing on the X4 simulator.

Uses disposable SDs and a fresh process after sleep. No device is flashed.
The simulator does not model physical e-ink, SD reliability or ESP32 memory.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import fnmatch
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import zipfile

from epub_image_grayscale import crop, make_epub, read_bmp


def run_case(program, root, kind, timeout):
    name = kind + ('-timeout' if timeout else '-manual')
    sd = root / name
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    settings = {
        'language': 'EN', 'uiTheme': 1, 'uiThemeSchemaVersion': 3,
        'orientation': 0, 'fontFamily': 0, 'fontFamilySchemaVersion': 3,
        'fontSize': 18, 'fontSizeSchemaVersion': 3, 'textAntiAliasing': 0,
        'displayDay': 0, 'sleepTimeoutMinutes': 1 if timeout else 10,
        'sleepScreen': 1, 'quickResumeSleepScreen': 1 if timeout else 0,
    }
    state = {'readerActivityLoadCount': 0, 'showBootScreen': False}
    if kind == 'flashcards':
        # Place the flashcard app first on Home; then open its real browser.
        settings.update(flashcardsShortcut=0, flashcardsShortcutOrder=0,
                        appsHubShortcutOrder=21, flashcardStudyMode=3)
        for shortcut in ('browseFiles', 'stats', 'syncDay', 'settings', 'opdsBrowser'):
            settings[shortcut + 'Shortcut'] = 1
        (sd / 'cards.csv').write_text('front,back\nFirst question,First answer\n'
                                     'Second question,Second answer\nThird question,Third answer\n')
        presses = [(4000, 'ENTER'), (5500, 'ENTER'), (7000, 'ENTER'),
                   (8500, 'LEFT'), (10000, 'ENTER')]
        before = 11500
        expected = 'FlashcardReview'
    else:
        path = sd / ('book.' + kind)
        text = '\n\n'.join(f'Paragraph {i}. A different line for each page of the sleep regression. ' * 8
                           for i in range(25))
        if kind == 'epub':
            make_epub(path, False)
            with zipfile.ZipFile(path) as source:
                entries = {item.filename: source.read(item) for item in source.infolist()}
            entries['chapter.xhtml'] = ('<html xmlns="http://www.w3.org/1999/xhtml"><body>' +
                                       ''.join('<p>' + p + '</p>' for p in text.split('\n\n')) +
                                       '</body></html>').encode()
            with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as book:
                for filename, data in entries.items():
                    book.writestr(filename, data, compress_type=(zipfile.ZIP_STORED if filename == 'mimetype'
                                                                 else zipfile.ZIP_DEFLATED))
        else:
            path.write_text(text)
        state.update(openEpubPath='/' + path.name, lastSleepFromReader=True)
        presses = [(5500, 'DOWN')]
        before = 8000
        expected = 'EpubReader' if kind == 'epub' else 'TxtReader'
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps(state))
    wake = 78000 if timeout else before + 3500
    if not timeout:
        presses.append((before + 1000, 'SLEEP'))
    presses.append((wake, 'ENTER'))
    presses.append((wake + 12000, 'QUIT'))  # fail cleanly if sleep never occurred
    env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{time}:{key}' for time, key in presses),
               CROSSPOINT_SIM_SCREENSHOTS=f'{before}:{sd / "before.bmp"}',
               CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='9000:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE=f'7500:{sd / "after.bmp"}')
    retained_frame_seen = False
    with (sd / 'run.log').open('w') as log:
        with subprocess.Popen([str(program)], env=env, stdout=log, stderr=subprocess.STDOUT) as process:
            deadline = time.monotonic() + wake / 1000 + 30
            while process.poll() is None:
                retained_frame_seen |= (store / 'sleep_frame.bin').exists()
                if time.monotonic() >= deadline:
                    process.kill()
                    raise AssertionError(f'{name}: simulator timed out')
                time.sleep(0.1)
            assert process.returncode == 0, f'{name}: simulator exited {process.returncode}'
    log = (sd / 'run.log').read_text(errors='replace')
    assert log.count('Entering activity: ' + expected) == 2, f'{name}: reader was not restored'
    assert 'Entering activity: Sleep' in log, f'{name}: sleep path not exercised'
    if timeout:
        assert 'Auto-sleep triggered' in log, f'{name}: inactivity did not trigger sleep'
        assert retained_frame_seen, f'{name}: Quick Resume did not save a frame'
        assert not (store / 'sleep_frame.bin').exists(), f'{name}: retained frame was not consumed'
    before_pixels = read_bmp(sd / 'before.bmp')
    after_pixels = read_bmp(sd / 'after.bmp')
    # Exclude the reader footer and flashcard date/battery strip after rotation.
    bounds = ((0, 65, len(before_pixels[0]), len(before_pixels) - 35)
              if kind == 'flashcards' else (0, 0, 480, 740))
    original = crop(before_pixels, *bounds)
    assert any(pixel != (255, 255, 255) for pixel in original), f'{name}: blank initial screen'
    assert original == crop(after_pixels, *bounds), f'{name}: content or position changed on wake'
    saved = json.loads((store / 'state.json').read_text())
    if kind == 'flashcards':
        assert not saved.get('flashcardResumePath'), f'{name}: wake target was not consumed'
        assert 'Study session resumed: remaining=2 reviewed=1 side=back' in log, \
            f'{name}: queue, response count or visible side was not restored'
        progress_files = list((store / 'bookdata').rglob('flashcards.json'))
        assert len(progress_files) == 1, f'{name}: missing persisted ratings'
        cards = json.loads(progress_files[0].read_text())['cards']
        assert sum(card['seenCount'] for card in cards) == 1, f'{name}: response lost or counted twice'
        assert sum(card['successCount'] for card in cards) == 1, f'{name}: successful response lost'
    else:
        assert saved['openEpubPath'] == '/' + path.name, f'{name}: book route was lost'
    print('PASS ' + name, flush=True)
    return name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--only', action='append', help='Run matching case names (glob; repeatable)')
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='sleep-resume-', dir=args.output_parent)).resolve()
    print('Artifacts: ' + str(root), flush=True)
    cases = [(kind, timeout) for kind in ('epub', 'txt', 'flashcards') for timeout in (False, True)
             if not args.only or any(fnmatch.fnmatchcase(kind + ('-timeout' if timeout else '-manual'), pattern)
                                     for pattern in args.only)]
    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(lambda case: run_case(args.program.resolve(), root, *case), cases))
    (root / 'results.json').write_text(json.dumps({'passed': results, 'boots': len(results) * 2}, indent=2))


if __name__ == '__main__':
    main()
