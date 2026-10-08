"""Navigation/dispatch regression for an isolated, instrumented X4 simulator.

Apply navigation_fixture.patch in the simulator copy and rebuild first.
Each case uses a disposable SD; no real book/settings/device data is touched.
"""

import argparse
import fnmatch
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


APPS = [
    ('browseFiles', 'FileBrowser'), ('syncDay', 'SyncDay'), ('settings', 'Settings'),
    ('readingStats', 'ReadingStats'), ('readingHeatmap', 'ReadingHeatmap'),
    ('readingProfile', 'ReadingProfile'), ('achievements', 'Achievements'),
    ('ifFound', 'IfFound'), ('recentBooks', 'RecentBooks'), ('bookmarks', 'HighlightsApp'),
    ('favorites', 'FavoritesApp'), ('flashcards', 'FlashcardsApp'), ('dictionary', 'Dictionary'),
    ('fileTransfer', 'CrossPointWebServer'), ('screenClean', 'ScreenClean'), ('sleep', 'SleepApp'),
    ('library', 'Library'), ('plugins', 'PluginCatalog'),
]
TABBED = {'Settings', 'TextSettings', 'Library', 'Achievements', 'FontSelect'}


def validate_trace(log):
    traces = re.findall(r'NAV\|([^|\n]+)\|(-?\d+)\|(-?\d+)\|(\d+)\|(\d+)', log)
    assert traces, 'No fixture trace; rebuild with navigation_fixture.patch'
    for name, logical, painted, count, offset in traces:
        logical, painted, count, offset = map(int, (logical, painted, count, offset))
        expected_offset = 1 if name in TABBED else 0
        assert offset == expected_offset, f'{name}: unexpected offset {offset}'
        if count:
            assert painted == logical - expected_offset, f'{name}: selection {logical}, painted {painted}'
    for name, selected, top, drawn in re.findall(r'DRAW\|([^|\n]+)\|(\d+)\|(\d+)\|(\d+)', log):
        selected, top, drawn = map(int, (selected, top, drawn))
        row = selected - (1 if name in TABBED else 0)
        if drawn and row >= 0:
            assert top <= row < top + drawn, f'{name}: selected row {row} outside viewport {top}+{drawn}'


def run_case(program, root, name, screen, presses, expected=None, setting=None, theme=1,
             language='EN', fitted_rows=None):
    sd = root / name
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    settings = {'language': language, 'uiTheme': theme, 'uiThemeSchemaVersion': 3, 'displayDay': 0,
                'orientation': 0, 'syncDayWifiChoice': 0, 'dateFormat': 0}
    for order, (key, _) in enumerate(APPS):
        settings[key + 'Shortcut'] = 1
        settings[key + 'ShortcutOrder'] = order
        settings[key + 'ShortcutVisible'] = 1
    (store / 'settings.json').write_text(json.dumps(settings))
    events = [(3500 + i * 400, key) for i, key in enumerate(presses)]
    action_time = 4000 + len(presses) * 400
    captures = [(action_time - 200, 'selected')]
    if expected or setting:
        events.append((action_time, 'ENTER'))
        captures.append((action_time + 1300, 'opened'))
    events.append((action_time + 2400, 'QUIT'))
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CPR_NAV_SCREEN=screen,
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{time}:{key}' for time, key in events),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{time}:{sd / (label + ".bmp")}' for time, label in captures))
    with (sd / 'run.log').open('w') as output:
        subprocess.run([str(program)], env=env, stdout=output, stderr=subprocess.STDOUT, timeout=40, check=True)
    log = (sd / 'run.log').read_text(errors='replace')
    validate_trace(log)
    if fitted_rows:
        activity, count = fitted_rows
        drawn = re.findall(rf'DRAW\|{activity}\|\d+\|(\d+)\|(\d+)', log)
        assert drawn and all((int(top), int(rows)) == (0, count) for top, rows in drawn), \
            f'{name}: short list clipped despite available screen space: {drawn}'
    if expected:
        assert f'Entering activity: {expected}' in log, f'{name}: did not open {expected}'
    if setting:
        saved = json.loads((store / 'settings.json').read_text())
        assert saved[setting] == 1, f'{name}: wrong setting changed'
    print(f'PASS {name}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--baseline', action='store_true', help='Run only the first case; old code must fail')
    parser.add_argument('--only', action='append', help='Run matching case names (glob; repeatable)')
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='list-nav-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)
    program = args.program.resolve()

    def run(name, screen, presses, **kwargs):
        if not args.only or any(fnmatch.fnmatchcase(name, pattern) for pattern in args.only):
            run_case(program, root, name, screen, presses, **kwargs)

    run('apps-cycle', 'apps', ['DOWN'] * len(APPS) + ['UP'])
    if args.baseline:
        return
    for index, (key, destination) in enumerate(APPS):
        presses = ['DOWN'] * index if index <= len(APPS) // 2 else ['UP'] * (len(APPS) - index)
        run(f'app-{key}', 'apps', presses, expected=destination)
    run('sync-set-date', 'sync', ['DOWN'], expected='ManualDate')
    run('sync-choose-wifi', 'sync', ['DOWN'] * 2, setting='syncDayWifiChoice')
    run('sync-timezone', 'sync', ['DOWN'] * 3, expected='TimezonePicker')
    run('sync-date-format', 'sync', ['UP'], setting='dateFormat')
    for screen in ('favorites', 'flashcards', 'dictionary', 'stats', 'clean', 'profiles', 'settings'):
        run(f'cycle-{screen}', screen, ['DOWN'] * 8 + ['UP'] * 8)
    run('apps-cycle-classic', 'apps', ['UP'] * len(APPS), theme=3)
    for theme in (3, 1):
        for language in ('EN', 'ES'):
            run(f'reader-menu-{theme}-{language}', 'reader-menu', ['DOWN'] * 19 + ['UP'] * 19,
                theme=theme, language=language)
            run(f'reader-menu-rotated-{theme}-{language}', 'reader-menu',
                ['DOWN'] * 9 + ['ENTER', 'DOWN', 'ENTER'] + ['DOWN'] * 19,
                theme=theme, language=language)
            run(f'sync-fit-{theme}-{language}', 'sync', ['DOWN'] * 5,
                     theme=theme, language=language, fitted_rows=('SyncDay', 5))
            run(f'bookstats-fit-{theme}-{language}', 'bookstats', ['DOWN'] * 3,
                     theme=theme, language=language, fitted_rows=('BookStatsActions', 3))
    print('PASS: navigation, viewport and destination matrix', flush=True)


if __name__ == '__main__':
    main()
