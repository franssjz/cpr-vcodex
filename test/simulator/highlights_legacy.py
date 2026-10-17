"""Legacy Korean highlight navigation and paint regression on the X4 simulator.

Uses normal reader/global Highlights navigation, a generated EPUB, legacy v4/v5
bookmarks.bin records, a cold chapter cache, and a fresh UI selection. No firmware
patch is needed.
The section cache is read only to discover the fixture's actual rendered pages
and word ranges. Screenshots are compared with a clean page and a correctly
indexed modern-text reference, so a successful jump alone cannot pass.
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
import zipfile
import zlib

from epub_image_grayscale import crop, png_chunk, read_bmp
from list_navigation import APPS


TARGET = '고요한바다 새벽별빛 조용한마음'
MISSING = '여기에는존재하지않는 아주특별한문장'
SECTION_HEADER = '<BifBBBBHHBBBBbBHIIIII'


def make_epub(path):
    paragraphs = []
    for index in range(32):
        if index == 12:
            paragraphs.append(f'<p>{TARGET}</p>')
        paragraphs.append(f'<p>문단{index:03d} 오늘은 작은 길을 따라 천천히 걸으며 새로운 이야기를 생각합니다. '
                          '책을 읽는 시간은 마음을 편안하게 합니다.</p>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as book:
        book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        book.writestr('META-INF/container.xml',
                      '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">'
                      '<rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/>'
                      '</rootfiles></container>')
        book.writestr('book.opf',
                      '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="uid">'
                      '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:identifier id="uid">legacy-korean-highlight-regression</dc:identifier>'
                      '<dc:title>Legacy Korean highlights</dc:title><dc:language>ko</dc:language></metadata>'
                      '<manifest><item id="ch" href="chapter.xhtml" media-type="application/xhtml+xml"/>'
                      '</manifest><spine><itemref idref="ch"/></spine></package>')
        book.writestr('chapter.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml">'
                      '<head><title>Legacy highlights</title></head><body>' + ''.join(paragraphs) + '</body></html>')


def read_pages(path, with_rows=False):
    data = path.read_bytes()
    header = struct.unpack_from(SECTION_HEADER, data)
    assert header[0] == 53, f'Fixture parser expects finalized section cache v53, got {header[0]}'
    count, lut = header[-6:-4]
    assert 5 <= count <= 30, f'Unexpected fixture page count {count}'
    offsets = struct.unpack_from('<' + 'I' * count, data, lut)
    pages = []
    for offset in offsets:
        elements, = struct.unpack_from('<H', data, offset)
        position = offset + 2
        words = []
        rows = []
        for _ in range(elements):
            tag, x, y = struct.unpack_from('<Bhh', data, position)
            assert tag == 1, 'The synthetic fixture must contain text lines only'
            position += 5
            size, focus, ruby, flags, text_size, ruby_size = struct.unpack_from('<HBBBHH', data, position)
            position += 9
            arena_arrays = size * (5 + 3 * focus + 2 * ruby + flags)
            text = data[position + arena_arrays:position + arena_arrays + text_size]
            line_words = [word.decode().lstrip('\u2003') for word in text.split(b'\0')[:-1]]
            assert len(line_words) == size
            words.extend(word for word in line_words if word.strip())
            rows.append([word for word in line_words if word.strip()])
            position += arena_arrays + text_size + ruby_size + 24  # serialized BlockStyle
        pages.append(rows if with_rows else words)
    return pages


def spans(words, snippet):
    needle = ''.join(snippet.split())
    found = []
    for start in range(len(words)):
        text = ''
        for end in range(start, len(words)):
            text += ''.join(words[end].split())
            if text == needle:
                found.append((start, end))
                break
            if not needle.startswith(text):
                break
    return found


def bookmark_bytes(page, snippet, start=0, end=1, end_page=None, visible_offset=None):
    raw = snippet.encode()
    version = 5 if visible_offset is not None else 4
    record = struct.pack('<BIHHBHHHH', version, 1, 0, page, 1, page if end_page is None else end_page,
                         start, end, len(raw)) + raw
    return record + struct.pack('<BI', 1, visible_offset) if version == 5 else record


def state(sd, reader=True):
    (sd / '.crosspoint/state.json').write_text(json.dumps({
        'openEpubPath': '/test.epub', 'lastSleepFromReader': reader,
        'readerActivityLoadCount': 0, 'showBootScreen': False,
    }))


def boot(program, sd, name, events, captures):
    env = os.environ.copy()
    for key in ('CPR_NAV_SCREEN', 'CPR_KOREADER_SCREEN', 'CROSSPOINT_SIM_FREE_HEAP', 'CROSSPOINT_SIM_MAX_ALLOC_HEAP'):
        env.pop(key, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(f'{time}:{key}' for time, key in events),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{time}:{sd / (label + ".bmp")}' for time, label in captures))
    with (sd / f'{name}.log').open('w') as log:
        subprocess.run([str(program)], env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=events[-1][0] / 1000 + 30, check=True)
    text = (sd / f'{name}.log').read_text(errors='replace')
    assert not re.search(r'Guru Meditation|panic.ed|AddressSanitizer|Segmentation fault', text)
    return text


def pixels(path):
    rows = read_bmp(path)
    assert len(rows) == 800 and len(rows[0]) == 480
    return bytes(channel for pixel in crop(rows, 0, 0, 480, 735) for channel in pixel)


def prepare(program, root, font_dir):
    sd = root / 'base'
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    make_epub(sd / 'test.epub')
    shutil.copytree(font_dir, sd / 'fonts' / font_dir.name)
    (store / 'settings.json').write_text(json.dumps({
        'language': 'EN', 'uiTheme': 1, 'uiThemeSchemaVersion': 3,
        'readerMenuStyle': 0, 'fontFamily': 0, 'fontFamilySchemaVersion': 3,
        'fontSize': 18, 'fontSizeSchemaVersion': 3, 'orientation': 0,
        'textAntiAliasing': 0, 'displayDay': 0, 'refreshFrequency': 5,
        'sdFontFamilyName': font_dir.name, 'screenMargin': 8, 'bionicReading': 0,
        'darkMode': 0, 'bookmarksShortcut': 0, 'bookmarksShortcutVisible': 1, 'bookmarksShortcutOrder': 0,
    }))
    state(sd)
    events = [(6500 + i * 750, 'DOWN') for i in range(10)] + [(17000, 'BACK'), (19500, 'QUIT')]
    log = boot(program, sd, 'setup', events, [(16000, 'setup')])
    assert 'Entering activity: EpubReader' in log and f'Loaded SD font family: {font_dir.name}' in log
    return describe_fixture(sd, root)


def describe_fixture(sd, root):
    store = sd / '.crosspoint'
    files = list(store.glob('epub_*/sections/0.bin'))
    if not files:
        files = list(store.glob('**/sections/0.bin'))
    assert len(files) == 1, files
    pages = read_pages(files[0])
    matches = [(page, start, end) for page, words in enumerate(pages) for start, end in spans(words, TARGET)]
    assert len(matches) == 1, matches
    target, start, end = matches[0]
    assert 1 < target < len(pages) - 2
    assert any(len(word) > 1 and all('가' <= ch <= '힣' for ch in word) for word in pages[target]), 'Need current Hangul word grouping'
    data_dirs = list((store / 'bookdata').iterdir())
    assert len(data_dirs) == 1
    metadata = {'pages': len(pages), 'target_page': target, 'target_start': start, 'target_end': end,
                'bookdata': data_dirs[0].relative_to(sd).as_posix(), 'page_words': pages}
    data = files[0].read_bytes()
    visible_lut = struct.unpack_from(SECTION_HEADER, data)[-1]
    metadata['page_offsets'] = list(struct.unpack_from('<' + 'I' * len(pages), data, visible_lut))
    (root / 'fixture.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2))
    return sd, metadata


def copy_case(base, root, name, meta, page, record=None, reader=True):
    sd = root / name
    shutil.copytree(base, sd)
    bookdata = sd / meta['bookdata']
    marks = bookdata / 'bookmarks.bin'
    if marks.exists():
        marks.unlink()
    if record is not None:
        marks.write_bytes(record)
    (bookdata / 'epub_progress.bin').write_bytes(struct.pack('<HHH', 0, page, meta['pages']))
    state(sd, reader)
    return sd


def reference(program, base, root, name, meta, page, record=None):
    sd = copy_case(base, root, name, meta, page, record)
    boot(program, sd, 'reference', [(8000, 'QUIT')], [(6500, 'page')])
    actual = struct.unpack_from('<HH', (sd / meta['bookdata'] / 'epub_progress.bin').read_bytes())[1]
    assert actual == page, (name, actual, page)
    return pixels(sd / 'page.bmp')


def run_case(program, base, root, name, meta, saved, snippet, expected_page, expected_pixels,
             global_route=False, visible_offset=None, continuation=None, cold_cache=False):
    original_record = bookmark_bytes(saved, snippet, visible_offset=visible_offset,
                                     end_page=saved + 1 if continuation else saved)
    sd = copy_case(base, root, name, meta, 0, original_record, reader=not global_route)
    if cold_cache:
        assert global_route, 'Cold recovery must start through global Highlights before opening the reader'
        # Remove only the copied synthetic chapter cache inside this new case's
        # SD. Book metadata stays available to Home/Apps/Highlights, but opening
        # the selection must paginate the chapter from scratch.
        assert root.resolve() in sd.resolve().parents
        cached_sections = list((sd / '.crosspoint').glob('**/sections/0.bin*'))
        assert cached_sections, 'The prepared fixture must contain a chapter cache to remove'
        for cached in cached_sections:
            assert sd.resolve() in cached.resolve().parents and cached.is_file() and not cached.is_symlink()
            cached.unlink()
        assert not list((sd / '.crosspoint').glob('**/sections/0.bin*'))
    if global_route:
        # Put Highlights first in Apps, and leave Apps as the Home shortcut.
        settings_path = sd / '.crosspoint/settings.json'
        settings = json.loads(settings_path.read_text())
        for order, (key, _) in enumerate(APPS):
            settings.update({key + 'Shortcut': 1, key + 'ShortcutOrder': order + 1,
                             key + 'ShortcutVisible': 1})
        settings.update(bookmarksShortcutOrder=0, appsHubShortcutOrder=0)
        settings_path.write_text(json.dumps(settings))
        events = [(5000, 'DOWN'), (6000, 'ENTER'), (7500, 'ENTER'), (9000, 'ENTER'),
                  (11000, 'ENTER'), (16000, 'QUIT')]
        captures = [(10000, 'list'), (14500, 'selected')]
    else:
        events = [(4500, 'ENTER')] + [(5500 + i * 500, 'DOWN') for i in range(5)]
        events += [(8500, 'ENTER'), (10500, 'ENTER'), (15500, 'QUIT')]
        captures = [(9500, 'list'), (14000, 'selected')]
    if continuation:
        events[-1:] = [(15000, 'DOWN'), (19000, 'QUIT')]
        captures.append((17500, 'continuation'))
    log = boot(program, sd, 'selection', events, captures)
    actual_page = struct.unpack_from('<HH', (sd / meta['bookdata'] / 'epub_progress.bin').read_bytes())[1]
    before_selected = log.split('[SIM] Saved screenshot: ' + str(sd / 'selected.bmp'))[0]
    saved_pages = re.findall(r'Progress saved: Chapter \d+, Page (\d+)', before_selected)
    landing_page = int(saved_pages[-1]) if saved_pages else None
    selected_pixels = pixels(sd / 'selected.bmp')
    failures = []
    if 'Entering activity: Bookmarks' not in log:
        failures.append('Highlights list did not open')
    if global_route and ('Entering activity: Apps' not in log or 'Entering activity: HighlightsApp' not in log):
        failures.append('Global Highlights route did not open')
    final_expected_page = expected_page + 1 if continuation else expected_page
    if landing_page != expected_page:
        failures.append(f'selected landing page {landing_page}, expected {expected_page}')
    if actual_page != final_expected_page:
        failures.append(f'landed on page {actual_page}, expected {final_expected_page}')
    if selected_pixels != expected_pixels:
        failures.append('reader pixels differ from the correct reference (wrong page/range or missing paint)')
    if continuation and pixels(sd / 'continuation.bmp') != continuation:
        failures.append('continuation pixels differ from the expected highlighted next page')
    final_record = (sd / meta['bookdata'] / 'bookmarks.bin').read_bytes()
    if final_record != original_record:
        failures.append('original bookmarks.bin was rewritten')
    report = {'case': name, 'saved_page': saved, 'landing_page': landing_page, 'expected_landing_page': expected_page,
              'cold_cache': cold_cache,
              'continuation_page': actual_page if continuation else None,
              'expected_continuation_page': final_expected_page if continuation else None,
              'bookmarks_sha256': hashlib.sha256(final_record).hexdigest(),
              'bookmarks_unchanged': final_record == original_record,
              'screenshot_sha256': hashlib.sha256(selected_pixels).hexdigest(), 'failures': failures}
    (sd / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)
    return report


def resume_case(program, base, root, name, meta, page, record, expected_pixels):
    sd = copy_case(base, root, name, meta, page, record)
    # Boot straight into the reader. No Highlights activation may establish a
    # trusted range before the screenshot: this exercises passive recovery.
    log = boot(program, sd, 'resume', [(8000, 'QUIT')], [(6500, 'resumed')])
    actual_page = struct.unpack_from('<HH', (sd / meta['bookdata'] / 'epub_progress.bin').read_bytes())[1]
    saved_pages = [int(value) for value in re.findall(r'Progress saved: Chapter \d+, Page (\d+)', log)]
    actual_pixels = pixels(sd / 'resumed.bmp')
    final_record = (sd / meta['bookdata'] / 'bookmarks.bin').read_bytes()
    failures = []
    if 'Entering activity: EpubReader' not in log or 'Entering activity: Bookmarks' in log:
        failures.append('Resume did not open the reader directly without Highlights activation')
    if actual_page != page or any(value != page for value in saved_pages):
        failures.append(f'Passive recovery changed reading position: final={actual_page}, saved={saved_pages}')
    if actual_pixels != expected_pixels:
        failures.append('resumed reader pixels differ from the expected highlighted or clean reference')
    if final_record != record:
        failures.append('original bookmarks.bin was rewritten')
    report = {'case': name, 'resume_page': page, 'actual_page': actual_page,
              'bookmarks_unchanged': final_record == record,
              'bookmarks_sha256': hashlib.sha256(final_record).hexdigest(),
              'screenshot_sha256': hashlib.sha256(actual_pixels).hexdigest(), 'failures': failures}
    (sd / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)
    return report


def passive_resume_cases(program, base, root, meta, painted_target):
    page = meta['target_page']
    stale = bookmark_bytes(page + 1, ' '.join(TARGET.replace(' ', '')),
                           visible_offset=meta['page_offsets'][page + 1])
    recovered = resume_case(program, base, root, 'resume-stale-v5-anchor', meta, page, stale, painted_target)
    # Earlier records whose text is visible but whose anchors are far away must
    # not occupy every bounded recovery slot and hide a later nearby record.
    distant_page = meta['pages'] - 1
    assert distant_page - page > 3 and meta['target_start'] >= 7
    distant = [bookmark_bytes(distant_page,
                               ' '.join(meta['page_words'][page][meta['target_start'] - extra:meta['target_end'] + 1]),
                               visible_offset=meta['page_offsets'][distant_page]) for extra in range(8)]
    records = distant + [stale]
    combined = struct.pack('<BI', 5, len(records)) + b''.join(record[5:] for record in records)
    shortlisted = resume_case(program, base, root, 'resume-nearby-after-distant-anchors', meta,
                              page, combined, painted_target)
    # The final generated page contains one trailing phrase. It occurs only
    # once on this page, but also on other pages in the saved hint's ±3 window.
    # A page-local matcher would paint it; passive recovery must reject it.
    ambiguous_page = meta['pages'] - 1
    words = meta['page_words'][ambiguous_page]
    snippet = ' '.join(words)
    assert len(snippet.encode()) <= 512 and spans(words, snippet) == [(0, len(words) - 1)]
    other_matches = [p for p in range(max(0, ambiguous_page - 3), ambiguous_page)
                     if spans(meta['page_words'][p], snippet)]
    assert other_matches, 'Passive ambiguity fixture needs a unique local phrase repeated on a nearby page'
    clean = reference(program, base, root, 'resume-ambiguous-clean', meta, ambiguous_page)
    ambiguous = bookmark_bytes(ambiguous_page, snippet, 0, len(words) - 1,
                               visible_offset=meta['page_offsets'][ambiguous_page])
    rejected = resume_case(program, base, root, 'resume-locally-unique-but-ambiguous', meta,
                           ambiguous_page, ambiguous, clean)
    # The file format permits duplicate records. Eight rejected resolutions
    # fill the bounded cache; the ninth must not bypass full-window validation
    # through a page-local paint fallback just because it has no cache entry.
    repeated = struct.pack('<BI', 5, 9) + ambiguous[5:] * 9
    overflow_rejected = resume_case(program, base, root, 'resume-ambiguous-after-full-cache', meta,
                                    ambiguous_page, repeated, clean)
    return [recovered, shortlisted, rejected, overflow_rejected]


def fresh_selection(program, base, root, meta):
    # Subtract the unique preceding phrase's paint from a one-word-longer reference.
    # This leaves an exact golden image for only the selected repeated word,
    # without trusting old saved indices or duplicating renderer geometry.
    words = meta['page_words'][0]
    section = next((base / '.crosspoint').glob('**/sections/0.bin'))
    rows = read_pages(section, with_rows=True)[0]
    selected = sum(len(row) for row in rows[:len(rows) // 3])
    assert selected > 0 and words.count(words[selected]) > 1
    clean = reference(program, base, root, 'fresh-clean', meta, 0)
    first = reference(program, base, root, 'fresh-prefix', meta, 0,
                      bookmark_bytes(0, ' '.join(words[:selected]), 0, selected - 1))
    pair = reference(program, base, root, 'fresh-through-selection', meta, 0,
                     bookmark_bytes(0, ' '.join(words[:selected + 1]), 0, selected))
    assert first != clean and pair != first, 'Fresh-selection golden references must visibly paint'
    expected = bytes(c if f != c else p for c, f, p in zip(clean, first, pair))
    assert expected != clean
    sd = copy_case(base, root, 'fresh-repeated-selection', meta, 0)
    rows_png = b''.join(b'\0' + expected[y * 480 * 3:(y + 1) * 480 * 3] for y in range(735))
    (sd / 'expected.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
                                    png_chunk(b'IHDR', struct.pack('>IIBBBBB', 480, 735, 8, 2, 0, 0, 0)) +
                                    png_chunk(b'IDAT', zlib.compress(rows_png)) + png_chunk(b'IEND', b''))
    events = [(4500, 'ENTER')] + [(5500 + i * 500, 'DOWN') for i in range(7)]
    events += [(9500, 'ENTER'), (12000, 'ENTER'), (13000, 'ENTER')]
    # Reopen Highlights and activate the new record in the same reader/Section.
    events += [(15500, 'ENTER')] + [(16500 + i * 500, 'DOWN') for i in range(5)]
    events += [(19500, 'ENTER'), (21500, 'ENTER'), (24500, 'QUIT')]
    log = boot(program, sd, 'selection', events,
               [(14500, 'saved'), (20500, 'list'), (23000, 'reopened')])
    failures = []
    painted_pixels = {i // 3 for i, (c, e) in enumerate(zip(clean, expected)) if c != e}
    right_edge = max(i % 480 for i in painted_pixels)
    edge_differences = {}
    images = {label: pixels(sd / (label + '.bmp')) for label in ('saved', 'reopened')}
    for label in ('saved', 'reopened'):
        actual = images[label]
        differences = {i // 3 for i, (a, e) in enumerate(zip(actual, expected)) if a != e}
        # The multiword golden has one extra right-edge dither column in this
        # fixture (25 pixels). All interior/exterior pixels remain exact; only
        # missing golden paint on that single column is permitted.
        allowed = len(differences) <= 25 and all(
            i in painted_pixels and i % 480 == right_edge and actual[i * 3:i * 3 + 3] == clean[i * 3:i * 3 + 3]
            for i in differences)
        interior_painted = any(actual[i * 3:i * 3 + 3] != clean[i * 3:i * 3 + 3]
                               for i in painted_pixels if i % 480 < right_edge)
        edge_differences[label] = len(differences)
        if not allowed or not interior_painted:
            failures.append(label + ': repeated selection did not paint exactly its selected occurrence')
    if images['saved'] != images['reopened']:
        failures.append('Reopening Highlights changed the freshly selected range pixels')
    if 'Entering activity: Bookmarks' not in log or 'Entering activity: DictionaryWordSelect' not in log:
        failures.append('Required selection or Highlights activity did not open')
    data = (sd / meta['bookdata'] / 'bookmarks.bin').read_bytes()
    fields = struct.unpack_from('<BIHHBHHHH', data)
    snippet = data[18:18 + fields[-1]].decode()
    if fields[:8] != (5, 1, 0, 0, 1, 0, selected, selected) or snippet != words[selected]:
        failures.append('UI saved an unexpected record: ' + repr((fields, snippet)))
    expected_record = bookmark_bytes(0, words[selected], selected, selected, visible_offset=meta['page_offsets'][0])
    if data != expected_record:
        failures.append('Persisted fresh record differs from the selected range/text/anchor')
    report = {'case': 'fresh-repeated-selection', 'saved_page': 0,
              'bookmarks_sha256': hashlib.sha256(data).hexdigest(),
              'golden_right_edge_difference_pixels': edge_differences,
              'screenshot_sha256': hashlib.sha256(pixels(sd / 'saved.bmp')).hexdigest(), 'failures': failures}
    (sd / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--font-dir', type=Path, required=True)
    parser.add_argument('--output-parent', type=Path, default=Path(tempfile.gettempdir()))
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--base', type=Path, help='Reuse a prepared root from --prepare-only')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--fresh-only', action='store_true', help='Run only the UI-created repeated selection case')
    mode.add_argument('--cold-only', action='store_true', help='Run only global Highlights with a cold chapter cache')
    mode.add_argument('--resume-only', action='store_true', help='Run only direct reader resume recovery and ambiguity cases')
    args = parser.parse_args()
    program = args.program.resolve()
    root = Path(tempfile.mkdtemp(prefix='highlights-legacy-', dir=args.output_parent)).resolve()
    print(f'Artifacts: {root}', flush=True)
    if args.base:
        base = args.base.resolve() / 'base'
        meta = json.loads((args.base / 'fixture.json').read_text())
    else:
        base, meta = prepare(program, root, args.font_dir.resolve())
    (root / 'fixture.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2))
    if args.prepare_only:
        return
    if args.fresh_only:
        results = [fresh_selection(program, base, root, meta)]
        (root / 'result.json').write_text(json.dumps(results, indent=2))
        assert not results[0]['failures'], 'Fresh highlight regression failed; see result.json'
        print('PASS: fresh repeated selection stays painted after reopening Highlights', flush=True)
        return
    page = meta['target_page']
    modern = bookmark_bytes(page, TARGET, meta['target_start'], meta['target_end'])
    clean_target = reference(program, base, root, 'clean-target', meta, page)
    painted = reference(program, base, root, 'painted-target', meta, page, modern)
    assert painted != clean_target, 'The valid reference highlight was not visibly painted'
    if args.resume_only:
        results = passive_resume_cases(program, base, root, meta, painted)
        (root / 'result.json').write_text(json.dumps(results, indent=2))
        assert all(not result['failures'] for result in results), 'Passive resume regression failed; see result.json'
        print('PASS: direct reader resume recovers unique highlights and rejects nearby ambiguity without navigating', flush=True)
        return
    syllables = ' '.join(TARGET.replace(' ', ''))
    # At the supplied font size this phrase is on page 4, with a saved hint of 5.
    # Cold opening initially builds eight pages: the recovery window also needs
    # candidate page 8 and continuation page 9 before it can prove uniqueness.
    # The first activation must extend pagination and paint the exact range.
    assert page == 4 and meta['pages'] > 9, 'Cold regression requires its target just before the initial cache watermark'
    cold_result = run_case(program, base, root, 'global-cold-syllables-shifted', meta, page + 1,
                           syllables, page, painted, global_route=True, cold_cache=True)
    if args.cold_only:
        (root / 'result.json').write_text(json.dumps([cold_result], indent=2))
        assert not cold_result['failures'], 'Cold highlight regression failed; see result.json'
        print('PASS: first global Highlights activation recovers and paints with a cold chapter cache', flush=True)
        return
    clean_other = reference(program, base, root, 'clean-other', meta, page + 1)
    results = passive_resume_cases(program, base, root, meta, painted) + [
        cold_result,
        run_case(program, base, root, 'syllables-shifted-forward', meta, page + 1, syllables, page, painted),
        run_case(program, base, root, 'words-shifted-backward', meta, page - 1, TARGET, page, painted),
        run_case(program, base, root, 'v5-stale-page-anchor', meta, page + 1, syllables, page, painted,
                 visible_offset=meta['page_offsets'][page + 1]),
        run_case(program, base, root, 'syllables-wrong-indices', meta, page, syllables, page, painted),
        run_case(program, base, root, 'missing-text', meta, page + 1, MISSING, page + 1, clean_other),
        run_case(program, base, root, 'ambiguous-repeated-text', meta, page + 1, '오늘은', page + 1, clean_other),
        run_case(program, base, root, 'global-syllables-shifted', meta, page + 1, syllables, page, painted, True),
    ]
    # A phrase straddling a page boundary must paint both halves after selection.
    cross_page = page
    page_words = meta['page_words'][cross_page]
    first_start = max(index for index, word in enumerate(page_words) if word.startswith('문단'))
    first_words = page_words[first_start:]
    next_words = meta['page_words'][cross_page + 1][:3]
    first_text, next_text = ' '.join(first_words), ' '.join(next_words)
    first_paint = reference(program, base, root, 'painted-cross-first', meta, cross_page,
                            bookmark_bytes(cross_page, first_text, first_start, len(page_words) - 1))
    next_paint = reference(program, base, root, 'painted-cross-next', meta, cross_page + 1,
                           bookmark_bytes(cross_page + 1, next_text, 0, 2))
    assert first_paint != clean_target and next_paint != clean_other, 'Cross-page golden highlights must visibly paint'
    results.append(run_case(program, base, root, 'cross-page-shifted', meta, cross_page - 1,
                            first_text + ' ' + next_text, cross_page, first_paint, continuation=next_paint))
    results.append(fresh_selection(program, base, root, meta))
    (root / 'result.json').write_text(json.dumps(results, indent=2))
    assert all(not result['failures'] for result in results), 'Legacy highlight regression failed; see result.json'
    print('PASS: legacy Korean highlights locate and paint uniquely, with no stale-index fallback', flush=True)


if __name__ == '__main__':
    main()
