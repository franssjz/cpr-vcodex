"""Stress popup text/rows after applying popup_preview.patch to a simulator copy.

Never apply that fixture patch to the checkout used for device builds.
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
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='popup-stress-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)
    for orientation in (0, 1, 2, 3):
        for scenario in ('options', 'message'):
            sd = root / f'{scenario}-{orientation}'
            store = sd / '.crosspoint'
            store.mkdir(parents=True)
            (store / 'settings.json').write_text(json.dumps({
                'language': 'ES', 'uiTheme': 1, 'uiThemeSchemaVersion': 3,
                'displayDay': 0, 'orientation': 0,
            }))
            captures = [(3200, 'initial'), (4700, 'scrolled'), (8500, 'restored')]
            if scenario == 'options':
                events = [(4000, 'DOWN'), (7800, 'UP'), (9300, 'ENTER'), (11000, 'QUIT')]
                selected = 39
            else:
                events = [(4000, 'DOWN'), (5200, 'DOWN'), (6400, 'UP'), (7600, 'UP'),
                          (9200, 'ENTER'), (10600, 'ENTER'), (12000, 'QUIT')]
                captures.append((10000, 'actions'))
                selected = 0
            env = os.environ.copy()
            env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
                       CPR_POPUP_PREVIEW=scenario, CPR_POPUP_ORIENTATION=str(orientation),
                       CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{time}:{key}' for time, key in events),
                       CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{time}:{sd / (name + ".bmp")}' for time, name in captures))
            with (sd / 'run.log').open('w') as log:
                subprocess.run([str(args.program.resolve())], env=env, stdout=log, stderr=subprocess.STDOUT,
                               timeout=35, check=True)
            log = (sd / 'run.log').read_text()
            assert log.count('POPUP_SELECTED=') == 1, f'{sd}: hidden action/duplicate callback'
            assert f'POPUP_SELECTED={selected}' in log, f'{sd}: wrong selection'
            assert (sd / 'initial.bmp').read_bytes() == (sd / 'restored.bmp').read_bytes(), sd
            assert (sd / 'initial.bmp').read_bytes() != (sd / 'scrolled.bmp').read_bytes(), sd
            print(f'PASS {sd.name}: scroll, return and callback', flush=True)


if __name__ == '__main__':
    main()
