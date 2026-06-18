#!/usr/bin/env python3
"""
oui_builder.py — Converts the IEEE OUI text database to a sorted binary lookup table.

Usage:
    python oui_builder.py --input oui.txt --output oui.bin [--limit 30000]

Download the IEEE OUI list from:
    https://standards-oui.ieee.org/oui/oui.txt

Copy the output oui.bin to the root of the SD card before deployment.
"""

import argparse

ENTRY_SIZE = 26  # 3 bytes OUI + 23 bytes name (null-padded)


def parse_oui_txt(path, limit):
    entries = []
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            # Lines look like: "00-00-00   (hex)  XEROX CORPORATION"
            if '(hex)' in line:
                parts = line.strip().split('(hex)')
                if len(parts) != 2:
                    continue
                oui_str = parts[0].strip().replace('-', ':')
                name = parts[1].strip()[:22]  # Truncate to 22 chars + null

                try:
                    oui_bytes = bytes(int(x, 16) for x in oui_str.split(':'))
                except ValueError:
                    continue

                entries.append((oui_bytes, name))

                if limit and len(entries) >= limit:
                    break

    return entries


def build_binary(entries, output_path):
    # Sort by OUI as a 3-byte big-endian integer
    entries.sort(key=lambda e: (e[0][0] << 16) | (e[0][1] << 8) | e[0][2])

    with open(output_path, 'wb') as f:
        for oui_bytes, name in entries:
            name_encoded = name.encode('ascii', errors='replace')
            name_padded = name_encoded[:23].ljust(23, b'\x00')
            f.write(oui_bytes + name_padded)

    print(f"Written {len(entries)} entries to {output_path} "
          f"({len(entries) * ENTRY_SIZE / 1024:.1f} KB)")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', default='oui.bin')
    parser.add_argument('--limit', type=int, default=30000)
    args = parser.parse_args()

    entries = parse_oui_txt(args.input, args.limit)
    build_binary(entries, args.output)
