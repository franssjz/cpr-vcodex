"""Exercise Settings > Sleep screen on the X4 simulator, using an isolated SD.

Run in Linux/WSL: python3 test/simulator/popup_scroll.py /path/to/program
Screenshots cover the first/last option, wrap-around, and saved selection.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--theme', type=int, default=1)
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='popup-scroll-', dir=args.output_parent)).resolve()
    store = root / '.crosspoint'
    store.mkdir()
    (store / 'settings.json').write_text(json.dumps({
        'language': 'ES', 'sleepScreen': 0, 'uiTheme': args.theme, 'uiThemeSchemaVersion': 3,
        'displayDay': 0, 'orientation': 0,
    }))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
    events = [(2000, 'DOWN'), (2800, 'DOWN'), (3600, 'DOWN'), (4400, 'ENTER'),
              (5400, 'DOWN'), (6200, 'ENTER')]
    captures = [(7000, 'first')]
    events.extend((7800 + i * 700, 'DOWN') for i in range(12))
    captures.append((16500, 'last'))
    events.append((17200, 'DOWN'))
    captures.append((18000, 'wrap'))
    events.append((18800, 'UP'))
    captures.append((19600, 'reverse-wrap'))
    events.extend([(20400, 'ENTER'), (21900, 'QUIT')])
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(root),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{time + 2000}:{key}' for time, key in events),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{time + 2000}:{root / (name + ".bmp")}' for time, name in captures))
    print(f'Artifacts: {root}', flush=True)
    with (root / 'run.log').open('w') as log:
        subprocess.run([str(args.program.resolve())], env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=45, check=True)
    for _, name in captures:
        assert (root / (name + '.bmp')).stat().st_size > 10000, name
    saved = json.loads((store / 'settings.json').read_text())
    assert saved['sleepScreen'] == 12, f'Last option was not saved: {saved["sleepScreen"]}'
    assert (root / 'first.bmp').read_bytes() == (root / 'wrap.bmp').read_bytes(), 'Forward wrap changed first page'
    assert (root / 'last.bmp').read_bytes() == (root / 'reverse-wrap.bmp').read_bytes(), 'Reverse wrap changed last page'
    print('PASS: all 13 choices reachable; both wrap directions and selection verified', flush=True)


if __name__ == '__main__':
    main()
