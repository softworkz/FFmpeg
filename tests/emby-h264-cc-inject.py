#!/usr/bin/env python3
"""Inject A/53 closed caption SEI NAL unit into a raw H264 bitstream.

Reads a raw H264 Annex B bitstream and inserts a synthetic A/53 caption
SEI NAL unit before the first VCL NAL (IDR slice or non-IDR slice).
This produces an H264 file that triggers the closed captions detection
in the H264 parser (p->sei.a53_caption.buf_ref check).

Usage: python3 emby-h264-cc-inject.py <input.h264> <output.h264>
"""

import sys
import os


def build_a53_cc_sei_nalu():
    """Build an H264 SEI NAL unit containing A/53 closed caption data.

    The SEI message is user_data_registered_itu_t_t35 (payload_type=4)
    with ATSC A/53 caption data (GA94 identifier, cc_data type 0x03).
    """
    # SEI payload: user_data_registered_itu_t_t35
    payload = bytearray()
    payload.append(0xB5)            # itu_t_t35_country_code (USA)
    payload.extend(b'\x00\x31')     # itu_t_t35_provider_code (ATSC)
    payload.extend(b'GA94')         # user_identifier
    payload.append(0x03)            # user_data_type_code (cc_data)
    payload.append(0xC1)            # reserved=1, process_cc_data=1, zero=0, cc_count=1
    payload.append(0xFF)            # reserved
    payload.extend(b'\xFC\x80\x80')  # cc_valid=1, cc_type=0, data=0x80,0x80
    payload.append(0xFF)            # marker_bits

    # Build SEI RBSP: payload_type + payload_size + payload + trailing bits
    sei_rbsp = bytearray()
    sei_rbsp.append(4)              # payload_type = user_data_registered_itu_t_t35
    sei_rbsp.append(len(payload))   # payload_size
    sei_rbsp.extend(payload)
    sei_rbsp.append(0x80)           # rbsp_trailing_bits

    # Apply emulation prevention: escape 0x00 0x00 {0x00, 0x01, 0x02, 0x03}
    escaped = bytearray()
    zero_count = 0
    for b in sei_rbsp:
        if zero_count >= 2 and b <= 0x03:
            escaped.append(0x03)    # emulation_prevention_three_byte
            zero_count = 0
        escaped.append(b)
        zero_count = zero_count + 1 if b == 0x00 else 0

    # Complete NAL unit: start code + nal_header(SEI=6) + escaped RBSP
    nalu = bytearray(b'\x00\x00\x00\x01')
    nalu.append(0x06)               # forbidden_zero_bit=0, nal_ref_idc=0, type=6 (SEI)
    nalu.extend(escaped)
    return bytes(nalu)


def find_nalu_boundaries(data):
    """Find start positions of all NAL units in Annex B bitstream."""
    positions = []
    i = 0
    while i < len(data) - 3:
        if data[i] == 0 and data[i+1] == 0:
            if data[i+2] == 1:
                positions.append((i, 3))   # 3-byte start code
                i += 3
                continue
            elif i < len(data) - 3 and data[i+2] == 0 and data[i+3] == 1:
                positions.append((i, 4))   # 4-byte start code
                i += 4
                continue
        i += 1
    return positions


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.h264> <output.h264>", file=sys.stderr)
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, 'rb') as f:
        data = f.read()

    boundaries = find_nalu_boundaries(data)
    if not boundaries:
        print("Error: no NAL units found", file=sys.stderr)
        sys.exit(1)

    # Find the first VCL NAL unit (types 1-5: non-IDR slice, IDR slice, etc.)
    vcl_types = {1, 2, 3, 4, 5}
    insert_pos = None
    for pos, sc_len in boundaries:
        nal_header = data[pos + sc_len]
        nal_type = nal_header & 0x1F
        if nal_type in vcl_types:
            insert_pos = pos
            break

    if insert_pos is None:
        print("Error: no VCL NAL unit found", file=sys.stderr)
        sys.exit(1)

    sei_nalu = build_a53_cc_sei_nalu()

    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(data[:insert_pos])
        f.write(sei_nalu)
        f.write(data[insert_pos:])


if __name__ == '__main__':
    main()
