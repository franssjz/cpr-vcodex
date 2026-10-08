"""Back must unwind one visible level per press on an isolated X4 simulator.

Apply navigation_fixture.patch only in the simulator checkout. Each case has
its own disposable SD and checks the live activity stack, not just entry logs.
"""

import argparse
import fnmatch
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from list_navigation import APPS


def run_case(program, root, name, presses, expected, opened, hold=120, theme=1,
             screen='apps', initial=None, back_key='BACK', back_count=1, opds=False):
    sd = root / name
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    settings = {'language': 'EN', 'uiTheme': theme, 'uiThemeSchemaVersion': 3,
                'displayDay': 0, 'orientation': 0, 'syncDayWifiChoice': 0}
    for order, (key, _) in enumerate(APPS):
        settings.update({key + 'Shortcut': 1, key + 'ShortcutOrder': order,
                         key + 'ShortcutVisible': 1})
    settings.update(initial or {})
    if opds:
        settings.update(opdsBrowserShortcut=1, opdsBrowserShortcutOrder=18, opdsBrowserShortcutVisible=1)
        (store / 'opds.json').write_text(json.dumps({'servers': [
            {'name': f'Local test {index}', 'url': f'http://127.0.0.1:1/{index}'} for index in range(2)
        ]}))
    (store / 'settings.json').write_text(json.dumps(settings))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
    events = [(6000 + i * 850, key) for i, key in enumerate(presses)]
    back_time = 6000 + len(presses) * 850
    for index in range(back_count):
        events.append((back_time + index * (hold + 150), f'{back_key}:{hold}'))
    end = back_time + back_count * (hold + 150) + 1800
    events.append((end + 300, 'QUIT'))
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CPR_NAV_SCREEN=screen,
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{t}:{key}' for t, key in events),
               CROSSPOINT_SIM_SCREENSHOTS=f'{back_time - 200}:{sd / "before.bmp"};{end}:{sd / "after.bmp"}')
    with (sd / 'run.log').open('w') as output:
        subprocess.run([str(program)], env=env, stdout=output, stderr=subprocess.STDOUT,
                       timeout=end / 1000 + 15, check=True)
    log = (sd / 'run.log').read_text(errors='replace')
    assert f'Entering activity: {opened}' in log, f'{name}: never opened {opened}'
    stack = []
    for action, activity in re.findall(r'(Entering|Exiting) activity: ([^\r\n]+)', log):
        if action == 'Entering':
            stack.append(activity)
        else:
            assert activity in stack, f'{name}: unbalanced exit {activity}'
            stack.remove(activity)
    assert stack and stack[-1] == expected, f'{name}: expected {expected}, got {stack}'
    if expected == 'Apps':
        painted = re.findall(r'DRAW\|Apps\|(\d+)\|', log)
        assert len(painted) >= 2 and painted[-1] == painted[-2], f'{name}: Apps lost its selection'
    assert (sd / 'after.bmp').stat().st_size > 10000
    assert not re.search(r'Guru Meditation|panic.ed|AddressSanitizer|Segmentation fault', log)
    print(f'PASS {name}: {opened} -> {expected}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--only', action='append')
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='back-nav-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)

    def run(name, presses, expected, opened, **kwargs):
        if not args.only or any(fnmatch.fnmatchcase(name, pattern) for pattern in args.only):
            run_case(args.program.resolve(), root, name, presses, expected, opened, **kwargs)

    def app(key):
        index = next(i for i, (item, _) in enumerate(APPS) if item == key)
        return (['DOWN'] * index if index <= len(APPS) // 2 else ['UP'] * (len(APPS) - index)) + ['ENTER']

    for hold in (120, 900):
        run(f'heatmap-{hold}', app('readingHeatmap'), 'Apps', 'ReadingHeatmap', hold=hold)
        run(f'manual-date-{hold}', app('syncDay') + ['DOWN', 'ENTER'], 'SyncDay', 'ManualDate', hold=hold)
    for key, activity in APPS:
        if key in ('readingHeatmap', 'fileTransfer'):
            continue
        run(f'app-{key}', app(key), 'Apps', activity)
    run('network-mode-cancel', app('fileTransfer'), 'Apps', 'NetworkModeSelection')
    run('nested-back-twice', app('syncDay') + ['DOWN', 'ENTER'], 'Apps', 'ManualDate', back_count=2)
    run('apps-to-home', [], 'Home', 'Apps')
    run('settings-root-to-home', [], 'Home', 'Settings', screen='settings')
    run('settings-popup-cancel', app('settings') + ['DOWN', 'ENTER'], 'Settings', 'Settings')
    run('clean-cancel', app('screenClean') + ['ENTER'], 'ScreenClean', 'ScreenClean')
    run('heatmap-classic', app('readingHeatmap'), 'Apps', 'ReadingHeatmap', theme=3)
    run('heatmap-remapped', app('readingHeatmap'), 'Apps', 'ReadingHeatmap', back_key='LEFT',
        initial={'frontButtonBack': 2, 'frontButtonLeft': 0})
    run('opds-picker-cancel', ['UP', 'ENTER'], 'Apps', 'OpdsServerList', opds=True)


if __name__ == '__main__':
    main()
