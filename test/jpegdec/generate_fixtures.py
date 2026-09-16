"""Generate tiny JPEGs with known DC-only pixels, without image dependencies.

Each 8x8 tile is constant gray. Progressive fixtures either interleave the
three DC components or put Y/Cb/Cr in separate scans (Crosspoint #2925).
AC coefficients are zero. No third-party images are included.
"""

from pathlib import Path
import struct
import sys


def segment(marker, payload):
    return bytes((255, marker)) + struct.pack('>H', len(payload) + 2) + payload


def entropy(values, baseline=False):
    bits = ''
    for difference in values:
        size = abs(difference).bit_length()
        bits += f'{size:04b}'
        if size:
            value = difference if difference >= 0 else difference + (1 << size) - 1
            bits += f'{value:0{size}b}'
        if baseline:
            bits += '0'  # AC table: EOB
    bits += '1' * (-len(bits) % 8)
    raw = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
    return raw.replace(b'\xff', b'\xff\x00')


def make_jpeg(progressive, separate=False, grayscale=False, distinct_cr_table=False):
    count = 1 if grayscale else 3
    data = b'\xff\xd8' + segment(0xDB, b'\x00' + bytes([1] * 64))
    components = b''.join(bytes((i + 1, 0x11, 0)) for i in range(count))
    data += segment(0xC2 if progressive else 0xC0, struct.pack('>BHHB', 8, 64, 64, count) + components)
    dc_counts = bytes([0, 0, 0, 12] + [0] * 12)
    data += segment(0xC4, b'\x00' + dc_counts + bytes(range(12)))
    if distinct_cr_table:
        data += segment(0xC4, b'\x01' + dc_counts + bytes(reversed(range(12))))
    data += segment(0xC4, b'\x10' + bytes([1] + [0] * 15) + b'\x00')
    previous = 0
    luma = []
    for y in range(8):
        for x in range(8):
            gray = 24 + ((x + 3 * y) % 8) * 28
            dc = (gray - 128) * 8
            luma.append(dc - previous)
            previous = dc
    scans = [[i] for i in range(count)] if separate else [list(range(count))]
    for scan in scans:
        selectors = b''.join(bytes((i + 1, 0x10 if i == 2 and distinct_cr_table else 0)) for i in scan)
        data += segment(0xDA, bytes([len(scan)]) + selectors + bytes((0, 0 if progressive else 63, 0)))
        if distinct_cr_table:
            # Encode each symbol separately but concatenate before padding.
            # For the interleaved DC scan, Cb=0 uses table 0 and Cr=0 table 1.
            bits = ''
            for delta in luma:
                size = abs(delta).bit_length()
                bits += f'{size:04b}'
                if size:
                    value = delta if delta >= 0 else delta + (1 << size) - 1
                    bits += f'{value:0{size}b}'
                bits += '0000' + '1011'
            bits += '1' * (-len(bits) % 8)
            raw = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
            data += raw.replace(b'\xff', b'\xff\x00')
        else:
            values = [luma[p] if i == 0 else 0 for p in range(64) for i in scan]
            data += entropy(values, baseline=not progressive)
    if progressive:
        for i in range(count):
            # Complete the AC scans explicitly so reference decoders do not
            # smooth missing coefficients in an unfinished progressive image.
            data += segment(0xDA, bytes((1, i + 1, 0, 1, 63, 0)))
            data += bytes(8)  # 64 EOB symbols, each encoded by one zero bit
    return data + b'\xff\xd9'


if __name__ == '__main__':
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    cases = {
        'baseline': (False, False, False, False),
        'progressive_interleaved': (True, False, False, False),
        'progressive_separate': (True, True, False, False),
        'progressive_gray': (True, False, True, False),
        'progressive_cr_table': (True, False, False, True),
    }
    for name, options in cases.items():
        (output / (name + '.jpg')).write_bytes(make_jpeg(*options))
