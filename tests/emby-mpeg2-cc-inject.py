#!/usr/bin/env python3
"""Inject A/53 closed caption user data into an MPEG-2 elementary stream.

Reads a raw MPEG-2 video elementary stream and inserts a synthetic user data
start code with A/53 caption data (GA94 identifier, cc_data type 0x03) after
each picture header. This produces an MPEG-2 file that triggers the A53 CC
parsing in ff_mpeg_decode_user_data().

Usage: python3 emby-mpeg2-cc-inject.py <input.m2v> <output.m2v>
"""

import sys
import os


USER_START_CODE = b'\x00\x00\x01\xb2'


def build_a53_cc_user_data():
    """Build MPEG-2 user data containing A/53 closed caption data.

    The user data follows the ATSC A/53 Part 4 specification:
    - GA94 identifier ("GA94")
    - user_data_type_code = 0x03 (cc_data)
    - cc_count = 1, process_cc_data_flag = 1
    - One cc_data_pkt with cc_valid=1, cc_type=0, data=0x80,0x80
    """
    ud = bytearray()
    ud.extend(b'GA94')
    ud.append(0x03)            # user_data_type_code: cc_data
    ud.append(0xC1)            # reserved=1, process_cc_data=1, zero=0, cc_count=1
    ud.append(0xFF)            # reserved
    ud.extend(b'\xFC\x80\x80')  # cc_valid=1, cc_type=0, data
    ud.append(0xFF)            # marker_bits
    return bytes(ud)


def find_start_codes(data):
    """Find all MPEG start code positions (00 00 01 xx)."""
    positions = []
    i = 0
    while i < len(data) - 3:
        if data[i] == 0 and data[i+1] == 0 and data[i+2] == 1:
            positions.append(i)
            i += 4
        else:
            i += 1
    return positions


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.m2v> <output.m2v>", file=sys.stderr)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, 'rb') as f:
        data = f.read()

    positions = find_start_codes(data)
    if not positions:
        print("Error: no start codes found", file=sys.stderr)
        sys.exit(1)

    PICTURE_START_CODE_BYTE = 0x00
    cc_user_data = USER_START_CODE + build_a53_cc_user_data()

    chunks = []
    prev = 0
    inserted = False
    for idx, pos in enumerate(positions):
        sc_type = data[pos + 3]
        if sc_type == PICTURE_START_CODE_BYTE and not inserted:
            next_sc = positions[idx + 1] if idx + 1 < len(positions) else len(data)
            chunks.append(data[prev:next_sc])
            chunks.append(cc_user_data)
            prev = next_sc
            inserted = True

    chunks.append(data[prev:])

    if not inserted:
        print("Error: no picture header found", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    with open(output_path, 'wb') as f:
        for chunk in chunks:
            f.write(chunk)


if __name__ == '__main__':
    main()
