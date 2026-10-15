"""KOReader settings, loopback HTTP, and rejected TLS preflight regressions.

Apply koreader_network_fixture.patch only in an isolated X4 simulator checkout.
All HTTP requests use a disposable loopback server; the fixture rejects public
URLs for network cases. No public accounts are created. Default URL cases only
open settings. ESP.restart() is a simulator no-op: recovery checks prove the
restart request / resumed guard, not RTC persistence, device TLS, or heap health.
Screenshot files are retained for visual review of URL and error text.
"""

import argparse
import fnmatch
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading

from epub_image_grayscale import crop, read_bmp


DEFAULT_URL = 'https://sync.koreader.rocks:443'
RESTART_LOG = 'Restarting once after rejected TLS preflight'


class LoopbackServer(ThreadingHTTPServer):
    def __init__(self):
        super().__init__(('127.0.0.1', 0), LoopbackHandler)
        self.requests = []
        self.response_status = 200
        self.response_body = '{}'
        self.drop_response = False


class LoopbackHandler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.respond()

    def do_POST(self):
        self.respond()

    def respond(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0'))).decode()
        self.server.requests.append({'method': self.command, 'path': self.path,
                                     'headers': dict(self.headers), 'body': body})
        if self.server.drop_response:
            self.close_connection = True
            return
        self.send_response(self.server.response_status)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(self.server.response_body.encode())


def seed(sd, urls, theme):
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    (store / 'settings.json').write_text(json.dumps({
        'language': 'EN', 'uiTheme': theme, 'uiThemeSchemaVersion': 3,
        'displayDay': 0, 'orientation': 0, 'koSyncAutoPullOnOpen': 0,
        'koSyncAutoPushOnClose': 0,
    }))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
    profiles = [dict(name=name, username=name, password='fixture-password', serverUrl=url,
                     matchMethod=0, sendMetadata=False, syncBehavior=0, serverType=1)
                for name, url in zip(('fixture-active', 'fixture-edited'), urls)]
    (store / 'koreader_profiles.json').write_text(json.dumps({'profiles': profiles, 'activeIndex': 0}))
    return profiles


def run_case(program, root, name, server, theme):
    sd = root / name
    screen = 'settings' if name.startswith('settings') else 'edit' if name.startswith('edit') else name.split('-')[0]
    if 'resumed' in name:
        screen += '-resumed'
    low_heap = name.endswith('lowheap')
    ui = screen in ('settings', 'edit')
    local = f'{"https" if low_heap else "http"}://127.0.0.1:{server.server_port}'
    urls = [local, local]
    if ui:
        index = 0 if screen == 'settings' else 1
        urls[index] = '' if name.endswith('default') else f'127.0.0.1:{server.server_port}/'
        urls[1 - index] = local if name.endswith('default') else ''
    elif not low_heap:
        # A different active server must not redirect inactive-profile sign-up.
        urls[0 if screen.startswith('signup') else 1] = 'http://127.0.0.1:1'
    profiles = seed(sd, urls, theme)
    server.requests.clear()
    server.drop_response = name == 'signup-transport-drop'
    server.response_status = 201 if name == 'signup-ok' else 401 if name == 'auth-rejected' else 402 if name == 'signup-exists' else 200
    server.response_body = ('{"code":2002,"message":"Username is already registered."}'
                            if name == 'signup-exists' else '{}')
    env = os.environ.copy()
    for key in ('CPR_NAV_SCREEN', 'CROSSPOINT_SIM_HTTP_MOCK_ROOT', 'CROSSPOINT_SIM_FREE_HEAP',
                'CROSSPOINT_SIM_MAX_ALLOC_HEAP', 'CROSSPOINT_SIM_WIFI_CONNECT'):
        env.pop(key, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CPR_KOREADER_SCREEN=screen, CPR_KOREADER_PROFILE_INDEX='1',
               CROSSPOINT_SIM_INPUT_SCRIPT='7000:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'6000:{sd / "screen.bmp"}',
               HTTP_PROXY='', HTTPS_PROXY='', ALL_PROXY='', NO_PROXY='127.0.0.1',
               http_proxy='', https_proxy='', all_proxy='', no_proxy='127.0.0.1')
    if low_heap:
        env.update(CROSSPOINT_SIM_FREE_HEAP='80000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='12000')
    with (sd / 'run.log').open('w') as log:
        subprocess.run([str(program)], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=25, check=True)
    log = (sd / 'run.log').read_text(errors='replace')
    expected_activity = 'KOReaderSettings' if screen == 'settings' else 'KOReaderProfileEdit' if screen == 'edit' else 'KOReaderAuth'
    assert f'Entering activity: {expected_activity}' in log, f'{name}: fixture did not open {expected_activity}'
    assert not re.search(r'Guru Meditation|panic.ed|AddressSanitizer|Segmentation fault', log)
    actual = json.loads((sd / '.crosspoint/koreader_profiles.json').read_text())
    assert actual['activeIndex'] == 0, f'{name}: changed the active profile'
    for before, after in zip(profiles, actual['profiles']):
        for field in ('name', 'username', 'serverUrl', 'matchMethod', 'sendMetadata', 'syncBehavior', 'serverType'):
            assert after[field] == before[field], f'{name}: changed profile field {field}'
    assert len(actual['profiles']) == len(profiles)
    requests = list(server.requests)
    if ui or low_heap:
        assert requests == [], f'{name}: unexpected network request'
    else:
        assert len(requests) == 1, f'{name}: expected one request, got {requests}'
        request = requests[0]
        expected_user = 'fixture-edited' if screen.startswith('signup') else 'fixture-active'
        password_key = hashlib.md5(b'fixture-password').hexdigest()
        if screen.startswith('signup'):
            assert request['method'] == 'POST' and request['path'] == '/users/create/'
            assert json.loads(request['body']) == {'username': expected_user, 'password': password_key}
        else:
            assert request['method'] == 'GET' and request['path'] == '/users/auth'
            headers = {key.lower(): value for key, value in request['headers'].items()}
            assert headers['x-auth-user'] == expected_user and headers['x-auth-key'] == password_key
        assert 'fixture-password' not in log and password_key not in log, f'{name}: leaked password/key to logs'
    restarts = log.count(RESTART_LOG)
    assert restarts == (1 if low_heap and 'resumed' not in screen else 0), f'{name}: unexpected restart count {restarts}'
    if low_heap:
        assert 'Insufficient contiguous heap for TLS: 12000 available' in log
    rows = read_bmp(sd / 'screen.bmp')
    pixels = crop(rows, 0, 75, 480, 735)
    assert sum(max(pixel) < 128 for pixel in pixels) > 300, f'{name}: empty screenshot'
    result = {'case': name, 'theme': theme, 'requests': len(requests), 'restart_requests': restarts,
              'active_profile': actual['activeIndex'], 'screenshot_sha256': hashlib.sha256(
                  bytes(channel for pixel in pixels for channel in pixel)).hexdigest(),
              'default_url': DEFAULT_URL if name.endswith('default') else None,
              'restart_emulated': False}
    (sd / 'result.json').write_text(json.dumps(result, indent=2))
    print(f'PASS {name}: {len(requests)} loopback requests, {restarts} restart requests', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--only', action='append', help='Optional case glob, repeatable')
    parser.add_argument('--theme', type=int, choices=(0, 1, 3), default=1)
    args = parser.parse_args()
    root = Path(tempfile.mkdtemp(prefix='koreader-network-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)
    server = LoopbackServer()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    results = []
    try:
        for name in ('settings-default', 'edit-default', 'settings-custom', 'edit-custom',
                     'auth-ok', 'auth-rejected', 'signup-ok', 'signup-exists', 'signup-transport-drop',
                     'auth-lowheap', 'signup-lowheap', 'auth-resumed-lowheap', 'signup-resumed-lowheap'):
            if not args.only or any(fnmatch.fnmatchcase(name, pattern) for pattern in args.only):
                results.append(run_case(args.program.resolve(), root, name, server, args.theme))
    finally:
        server.shutdown()
        server.server_close()
        (root / 'result.json').write_text(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
