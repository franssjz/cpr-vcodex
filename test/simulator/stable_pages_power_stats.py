"""Issue #207: CrossInk page totals, power shortcuts, and return-to-reader on X4.

Run on Linux/WSL against the normal simulator binary. Uses disposable SD cards;
retains screenshots and logs for inspection. No physical panel/power validation.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

from epub_image_grayscale import crop, read_bmp


def make_book(path, metadata):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as book:
        book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        book.writestr('META-INF/container.xml',
                      '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">'
                      '<rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/>'
                      '</rootfiles></container>')
        book.writestr('book.opf',
                      '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="uid">'
                      '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:identifier id="uid">issue-207-regression</dc:identifier>'
                      '<dc:title>Stable page numbers</dc:title><dc:language>en</dc:language></metadata>'
                      '<manifest><item id="ch" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest>'
                      '<spine><itemref idref="ch"/></spine></package>')
        paragraph = ('Stable pages use the same reference units at every font size. '
                     'Reading statistics can be opened with a long power press. ')
        book.writestr('chapter.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Test</title></head>'
                      '<body>' + ''.join(f'<p>{paragraph * 4}</p>' for _ in range(60)) + '</body></html>')
        if metadata != 'missing':
            manifest = {'format': 'x-locations', 'version': 1, 'totalCharacters': 60000,
                        'charactersPerReferencePage': 1500, 'totalReferencePages': 40,
                        'spine': [{'index': 0, 'characterStart': 0, 'characterCount': 60000}]}
            if metadata == 'invalid':
                manifest['spine'][0]['characterCount'] = 60001
            book.writestr('META-INF/x-locations.json', json.dumps(manifest))


def run_case(program, root, name, metadata='valid', settings=None, events=None, captures=None, heap=None):
    sd = root / name
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    make_book(sd / 'test.epub', metadata)
    config = {'language': 'EN', 'uiTheme': 1, 'uiThemeSchemaVersion': 3,
              'fontFamily': 0, 'fontFamilySchemaVersion': 3, 'fontSize': 18, 'fontSizeSchemaVersion': 3,
              'bionicReading': 2, 'orientation': 0, 'textAntiAliasing': 0, 'displayDay': 0,
              'stablePageNumbers': 1, 'statusBarChapterPageCount': 1,
              'statusBarBookProgressPercentage': 0, 'statusBarBattery': 0, 'statusBarTitle': 2,
              'statusBarProgressBar': 2, 'longPwrBtn': 1, 'shortPwrBtn': 2,
              'showStatsAfterReading': 0, 'readingStatsAutoBackup': 0}
    config.update(settings or {})
    (store / 'settings.json').write_text(json.dumps(config))
    (store / 'state.json').write_text(json.dumps({'openEpubPath': '/test.epub', 'lastSleepFromReader': True,
                                                'readerActivityLoadCount': 0, 'showBootScreen': False}))
    events = events or '8500:QUIT'
    captures = captures or {'page': 7500}
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=events,
               CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='3500:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{sd / (key + ".bmp")}' for key, ms in captures.items()))
    if heap:
        env.update(CROSSPOINT_SIM_FREE_HEAP=str(heap), CROSSPOINT_SIM_MAX_ALLOC_HEAP=str(heap))
    with (sd / 'run.log').open('w') as output:
        subprocess.run([str(program)], env=env, stdout=output, stderr=subprocess.STDOUT, timeout=50, check=True)
    log = (sd / 'run.log').read_text(errors='replace')
    assert 'Entering activity: EpubReader' in log, name
    loaded = metadata == 'valid' and config['stablePageNumbers'] and not heap
    assert ('Loaded stable reference pages' in log) == bool(loaded), name
    assert not list(store.rglob('reference-pages.tmp')), name
    pictures = {key: read_bmp(sd / (key + '.bmp')) for key in captures if (sd / (key + '.bmp')).exists()}
    print(f'PASS boot {name}', flush=True)
    return sd, log, pictures


def settings_case(program, root):
    # Exit to Home, open its first shortcut, switch to Reader, wrap to the last
    # row (status bar), toggle stable pages, then select the Controls power row.
    events = ['6500:BACK', '10000:DOWN', '11200:ENTER', '12400:ENTER', '13600:UP',
              '14800:ENTER', '16000:DOWN', '17200:ENTER', '19000:BACK', '20200:BACK', '21400:ENTER']
    events += [f'{22600 + i * 1000}:DOWN' for i in range(7)]
    events += ['30000:ENTER', '35000:QUIT']
    sd, log, _ = run_case(program, root, 'device-settings', settings={
        'stablePageNumbers': 0, 'longPwrBtn': 0, 'settingsShortcutOrder': 0,
        'browseFilesShortcut': 1, 'syncDayShortcut': 1,
    }, events=';'.join(events), captures={'stable-setting': 18100, 'power-setting': 33600})
    saved = json.loads((sd / '.crosspoint' / 'settings.json').read_text())
    assert saved['stablePageNumbers'] == 1, 'status bar option not saved'
    assert saved['longPwrBtn'] == 1, 'power option not saved'
    assert 'Entering activity: StatusBarSettings' in log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--only-settings', action='store_true')
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='stable-pages-power-', dir=args.output_parent)).resolve()
    program = args.program.resolve()
    print(f'Artifacts: {root}', flush=True)
    if args.only_settings:
        settings_case(program, root)
        print('PASS device settings persistence', flush=True)
        return
    results = {}
    for name, metadata, settings, heap in [
        ('font18', 'valid', {}, None),
        ('font24', 'valid', {'fontSize': 24}, None),
        ('missing', 'missing', {}, None),
        ('invalid', 'invalid', {}, None),
        ('disabled', 'valid', {'stablePageNumbers': 0}, None),
        ('low-heap', 'valid', {}, 70000),
    ]:
        _, _, pictures = run_case(program, root, name, metadata, settings, heap=heap)
        results[name] = pictures['page']
    # Same fixed footer at two font sizes; malformed/absent metadata uses the
    # same chapter counter as the disabled setting.
    footer = lambda image: crop(image, 310, 738, 479, 799)
    assert footer(results['font18']) == footer(results['font24']), 'reference total changed with font size'
    assert footer(results['missing']) == footer(results['invalid']) == footer(results['disabled']), 'fallback differs'
    assert footer(results['font18']) != footer(results['missing']), 'reference footer not displayed'

    sd, log, pictures = run_case(program, root, 'hold-return', events=(
        '6500:POWER:2200;10500:POWER:700;12500:BACK;15500:BACK;17500:QUIT'),
        captures={'before': 5500, 'stats': 9500, 'returned': 14500, 'home': 16500})
    assert log.count('Entering activity: ReadingStats\n') == 1, 'repeated hold duplicated stats'
    assert 'Power button held' not in log, 'hold unexpectedly slept'
    assert crop(pictures['before'], 0, 0, 480, 730) == crop(pictures['returned'], 0, 0, 480, 730), 'reader moved'
    assert crop(pictures['before'], 0, 0, 480, 730) != crop(pictures['stats'], 0, 0, 480, 730), 'stats not shown'
    assert 'Exiting activity: ReadingStats' in log, 'Back did not close stats'
    # The final Back closes the reader, flushing the reading session. Its time
    # should exclude the six seconds spent in statistics (including the hold).
    stats = json.loads((sd / '.crosspoint' / 'reading_stats.json').read_text())
    reading_ms = stats['books'][0]['totalReadingMs']
    assert 4000 <= reading_ms < 10000, f'statistics visit counted as reading: {reading_ms}ms'
    assert sum(day['readingMs'] for day in stats['readingDays']) == reading_ms, 'daily total differs'

    for name, short, events, expect_stats in [
        ('short-turn', 2, '6500:POWER:80;9500:QUIT', False),
        ('short-sleep', 1, '6500:POWER:80;9500:QUIT', False),
        ('hold-with-short-sleep', 1, '6500:POWER:700;9500:QUIT', True),
        ('hold-with-short-refresh', 3, '6500:POWER:700;9500:QUIT', True),
        ('sleep-from-stats', 1, '6500:POWER:700;9500:POWER:80;12000:QUIT', True),
    ]:
        sd, log, _ = run_case(program, root, name, settings={'shortPwrBtn': short}, events=events)
        assert ('Entering activity: ReadingStats' in log) == expect_stats, name
        if expect_stats:
            assert 'Manual screen refresh triggered' not in log and 'Power button held' not in log, name
        if name == 'short-sleep':
            assert 'Entering activity: Sleep' in log, name
        if name == 'short-turn':
            assert 'Progress saved: Chapter 0, Page 1,' in log, name
        if name == 'sleep-from-stats':
            assert 'Entering activity: Sleep' in log, name
            stats = json.loads((sd / '.crosspoint' / 'reading_stats.json').read_text())
            assert 2000 <= stats['books'][0]['totalReadingMs'] < 7000, 'sleep credited the paused visit'
    _, log, _ = run_case(program, root, 'default-hold-sleep', settings={'longPwrBtn': 0},
                        events='6500:POWER:700;9500:QUIT')
    assert 'Entering activity: Sleep' in log and 'Entering activity: ReadingStats' not in log
    settings_case(program, root)
    (root / 'result.json').write_text(json.dumps({'result': 'PASS', 'cases': 14}, indent=2))
    print('PASS stable page footer and power shortcut regressions', flush=True)


if __name__ == '__main__':
    main()
