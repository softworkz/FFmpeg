#!/usr/bin/env python3
"""Generate test MP3 files with specific ID3v2 tag structures for FATE testing.

Creates minimal MP3 files (ID3v2 header + silent MPEG frames) with carefully
crafted tag structures to test the custom ID3v2 parsing behaviors in
libavformat/id3v2.c.

Usage: python3 emby-id3v2-gen.py <test-id> <output-path>

Test IDs:
  genre-paren   - ID3v2.3 TCON with "(17)"
  genre-numeric - ID3v2.4 TCON with "17"
  genre-nonfull - ID3v2.4 TCON with "17abc"
  multivalue    - ID3v2.3 TPE1 with null-separated values
  duplicate     - ID3v2.3 with two TIT2 frames
  genre-multi   - ID3v2.3 TCON with null-separated genre numbers
"""

import struct
import sys
import os


def syncsafe_encode(n):
    """Encode integer as 4-byte syncsafe integer (7 bits per byte)."""
    result = bytearray(4)
    for i in range(3, -1, -1):
        result[i] = n & 0x7F
        n >>= 7
    return bytes(result)


def id3v2_tag(version, frames_data):
    """Build complete ID3v2 tag."""
    return b'ID3' + bytes([version, 0, 0]) + syncsafe_encode(len(frames_data))  \
           + frames_data


def frame_v23(frame_id, data):
    """Build ID3v2.3 frame (size is regular big-endian, not syncsafe)."""
    return frame_id.encode('ascii') + struct.pack('>I', len(data)) \
           + b'\x00\x00' + data


def frame_v24(frame_id, data):
    """Build ID3v2.4 frame (size is syncsafe)."""
    return frame_id.encode('ascii') + syncsafe_encode(len(data)) \
           + b'\x00\x00' + data


def text_data(text, encoding=0):
    """Build text frame payload: encoding byte + text."""
    return bytes([encoding]) + text.encode('latin-1')


def text_data_multi(values, encoding=0):
    """Build multi-value text frame payload with null separators."""
    data = bytes([encoding])
    for i, val in enumerate(values):
        if i > 0:
            data += b'\x00'
        data += val.encode('latin-1')
    return data


def mp3_frame():
    """Minimal valid MPEG1 Layer 3 frame (128 kbps, 44100 Hz, mono, silence).

    Header: 0xFF 0xFB 0x90 0xC0
      sync=0xFFF, MPEG1, Layer3, no CRC, 128kbps, 44100Hz, mono
    Frame size = floor(144 * 128000 / 44100) = 417 bytes.
    """
    header = bytes([0xFF, 0xFB, 0x90, 0xC0])
    return header + b'\x00' * (417 - 4)


def write_mp3(path, id3_data):
    """Write MP3 file: ID3v2 tag + two silent MPEG frames."""
    with open(path, 'wb') as f:
        f.write(id3_data)
        f.write(mp3_frame())
        f.write(mp3_frame())


GENERATORS = {}


def generator(name):
    def decorator(func):
        GENERATORS[name] = func
        return func
    return decorator


@generator('genre-paren')
def gen_genre_paren(path):
    """ID3v2.3, TCON="(17)" -- parenthesized genre number."""
    frames = frame_v23('TCON', text_data('(17)'))
    write_mp3(path, id3v2_tag(3, frames))


@generator('genre-numeric')
def gen_genre_numeric(path):
    """ID3v2.4, TCON="17" -- bare numeric genre (v2.4 convention)."""
    frames = frame_v24('TCON', text_data('17'))
    write_mp3(path, id3v2_tag(4, frames))


@generator('genre-nonfull')
def gen_genre_nonfull(path):
    """ID3v2.4, TCON="17abc" -- partial numeric, should NOT resolve."""
    frames = frame_v24('TCON', text_data('17abc'))
    write_mp3(path, id3v2_tag(4, frames))


@generator('multivalue')
def gen_multivalue(path):
    """ID3v2.3, TPE1 with null-separated multi-values."""
    frames = frame_v23('TPE1', text_data_multi(['Artist1', 'Artist2', 'Artist3']))
    write_mp3(path, id3v2_tag(3, frames))


@generator('duplicate')
def gen_duplicate(path):
    """ID3v2.3, two TIT2 frames -- second should be ignored."""
    frames = frame_v23('TIT2', text_data('First'))
    frames += frame_v23('TIT2', text_data('Second'))
    write_mp3(path, id3v2_tag(3, frames))


@generator('genre-multi')
def gen_genre_multi(path):
    """ID3v2.3, TCON with null-separated genre numbers."""
    frames = frame_v23('TCON', text_data_multi(['(17)', '(0)']))
    write_mp3(path, id3v2_tag(3, frames))


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <test-id> <output-path>", file=sys.stderr)
        print(f"Test IDs: {', '.join(sorted(GENERATORS.keys()))}", file=sys.stderr)
        sys.exit(1)

    test_id = sys.argv[1]
    output_path = sys.argv[2]

    if test_id not in GENERATORS:
        print(f"Unknown test ID: {test_id}", file=sys.stderr)
        print(f"Valid IDs: {', '.join(sorted(GENERATORS.keys()))}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    GENERATORS[test_id](output_path)


if __name__ == '__main__':
    main()
