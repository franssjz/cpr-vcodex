"""Exercise real OPDS Loading, parse failure, and recovery in one simulator boot.

Apply opds_feed_loading_fixture.patch only in an isolated X4 simulator copy.
The fixture and this server use loopback HTTP and a disposable simulated SD.
This verifies rendering/locking, navigation, and temp-file cleanup, not ESP32
heap pressure, SD hardware failure, or TLS allocation behavior.
"""

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import threading
import time

from epub_image_grayscale import crop, read_bmp


FEEDS = {
    '/root': '''<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom">
      <title>Fixture root</title>
      <entry><title>Small shelf</title><link type="application/atom+xml" href="/small"/></entry>
      <entry><title>Broken shelf</title><link type="application/atom+xml" href="/bad"/></entry>
    </feed>''',
    '/small': '''<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom">
      <title>Small shelf</title><entry><title>Recovered book</title><author><name>Fixture author</name></author>
      <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/book.epub"/></entry>
    </feed>''',
    '/bad': '''<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom">
      <entry><title>Partial entry</title><link type="application/atom+xml" href="/never"/></entry>
      <entry><title>Unclosed title</entry></feed>''',
}


class LoopbackServer(ThreadingHTTPServer):
    def __init__(self, sd):
        super().__init__(('127.0.0.1', 0), LoopbackHandler)
        self.requests = []
        self.sd = sd


class LoopbackHandler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        # The destination is created before HTTP starts. A previous response
        # must have been removed/truncated before the next request arrives.
        staged = self.server.sd / '.crosspoint/opds-feed.tmp'
        self.server.requests.append({'path': self.path,
                                     'staged_bytes_before_response': staged.stat().st_size if staged.exists() else 0})
        body = FEEDS.get(self.path, '').encode()
        # Keep the first request pending longer to exercise the Loading wait
        # from onEnter; later requests exercise ordinary navigation.
        time.sleep(4 if len(self.server.requests) == 1 else 1.4)
        self.send_response(200 if body else 404)
        self.send_header('Content-Type', 'application/atom+xml')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def read_framebuffer_bmp(path):
    data = path.read_bytes()
    assert data[:2] == b'BM', path
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    assert struct.unpack_from('<H', data, 28)[0] == 1
    palette_start = 14 + struct.unpack_from('<I', data, 14)[0]
    colors = [tuple(data[palette_start + i * 4:palette_start + i * 4 + 3]) for i in range(2)]
    stride = ((width + 31) // 32) * 4
    rows = []
    for y in range(abs(height)):
        source_y = height - 1 - y if height > 0 else y
        start = offset + source_y * stride
        rows.append([colors[(data[start + x // 8] >> (7 - x % 8)) & 1] for x in range(width)])
    return rows


def run(program, output_parent, theme):
    sd = Path(tempfile.mkdtemp(prefix='opds-feed-loading-', dir=output_parent)).resolve()
    print(f'Artifacts: {sd}', flush=True)
    store = sd / '.crosspoint'
    store.mkdir()
    (store / 'settings.json').write_text(json.dumps({
        'language': 'EN', 'uiTheme': theme, 'uiThemeSchemaVersion': 3,
        'displayDay': 0, 'orientation': 0,
    }))
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False}))
    # Seed stale content: the first response must replace it completely.
    (store / 'opds-feed.tmp').write_text('stale feed must never be parsed')
    server = LoopbackServer(sd)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    events = [(8000, 'ENTER'), (11000, 'BACK'), (14000, 'DOWN'), (14300, 'ENTER'),
              (17400, 'BACK'), (20300, 'ENTER'), (23500, 'QUIT')]
    captures = [(7500, 'root-first'), (10200, 'small-before'),
                (13200, 'root-before-bad'), (16600, 'error'),
                (19500, 'root-after-bad'), (22600, 'small-after')]
    env = os.environ.copy()
    for key in ('CPR_NAV_SCREEN', 'CPR_KOREADER_SCREEN', 'CROSSPOINT_SIM_HTTP_MOCK_ROOT',
                'CROSSPOINT_SIM_FREE_HEAP', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP', 'CROSSPOINT_SIM_WIFI_CONNECT'):
        env.pop(key, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CPR_OPDS_FEED_URL=f'http://127.0.0.1:{server.server_port}/root',
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{ms}:{key}' for ms, key in events),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{sd / (name + ".bmp")}' for ms, name in captures),
               HTTP_PROXY='', HTTPS_PROXY='', ALL_PROXY='', NO_PROXY='127.0.0.1',
               http_proxy='', https_proxy='', all_proxy='', no_proxy='127.0.0.1')
    try:
        with (sd / 'run.log').open('w') as log:
            subprocess.run([str(program)], env=env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=35, check=True)
    finally:
        server.shutdown()
        server.server_close()
        (sd / 'requests.json').write_text(json.dumps(server.requests, indent=2))
    log = (sd / 'run.log').read_text(errors='replace')
    assert log.count('Entering activity: OpdsBookBrowser') == 1, 'Activity did not stay in one OPDS session'
    assert not re.search(r'Guru Meditation|panic.ed|AddressSanitizer|Segmentation fault|Restarting|Silent restart', log)
    expected = ['/root', '/small', '/root', '/bad', '/root', '/small']
    assert [item['path'] for item in server.requests] == expected, server.requests
    assert all(item['staged_bytes_before_response'] == 0 for item in server.requests), 'Stale response was retained'
    assert not (store / 'opds-feed.tmp').exists(), 'Temporary feed remains after final success'
    assert 'Parse error at line' in log, 'Malformed response did not exercise the XML failure path'
    pixels = {}
    for _, name in captures:
        rows = read_bmp(sd / (name + '.bmp'))
        pixels[name] = bytes(channel for pixel in crop(rows, 0, 75, 480, 735) for channel in pixel)
    assert pixels['small-before'] == pixels['small-after'], 'Small shelf changed after the failed feed'
    assert pixels['root-before-bad'] == pixels['root-after-bad'], 'Back did not restore the root catalog'
    loading = []
    for index in range(1, len(expected) + 1):
        rows = read_framebuffer_bmp(sd / f'opds-loading-{index}.bmp')
        loading.append(bytes(channel for pixel in crop(rows, 0, 75, 480, 735) for channel in pixel))
    assert len(set(loading)) == 1, 'Loading screen was not rendered consistently'
    assert loading[0] != pixels['small-before'] != pixels['error'], 'Expected distinct Loading/list/error screens'
    assert sum(channel < 128 for channel in loading[0]) > 100, 'Loading snapshot was blank'
    pixels['loading'] = loading[0]
    result = {'theme': theme, 'requests': expected, 'one_session': True, 'temporary_feed_removed': True,
              'screenshots': {name: hashlib.sha256(data).hexdigest() for name, data in pixels.items()},
              'validates_hardware_heap_or_tls': False}
    (sd / 'result.json').write_text(json.dumps(result, indent=2))
    print('PASS OPDS: Loading rendered; small shelf -> malformed shelf -> small shelf recovered; temp file removed', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--theme', type=int, choices=(0, 1, 3), default=1)
    args = parser.parse_args()
    run(args.program.resolve(), args.output_parent, args.theme)


if __name__ == '__main__':
    main()
