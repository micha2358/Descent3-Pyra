#!/usr/bin/env python3
"""Audit user-supplied Descent 3 HOG2 game archives without modifying them.

Usage: ./check-pyra-assets.py /path/to/Descent3-data [other-data-directory ...]
This checks archives by their HOG2 index, as read by Descent 3's cfile.cpp.
"""
import argparse
from pathlib import Path
import struct
import sys

HUD_NAMES = (
    'ret_pr0a.ogf', 'ret_pr0b.ogf',
    'phx_pr0a.ogf', 'phx_pr0b.ogf',
    'phx_se1a.ogf', 'phx_se1b.ogf',
    'phx_se2a.ogf', 'phx_se2b.ogf',
)

def read_hog2(path):
    with path.open('rb') as f:
        header = f.read(12)
        if len(header) != 12 or header[:4] != b'HOG2':
            return None
        _, count, start = struct.unpack('<4sII', header)
        size = path.stat().st_size
        if count > 100000 or start > size or 68 + count * 48 > size:
            raise ValueError(f'invalid HOG2 index: {path}')
        f.seek(68)
        found = set()
        for _ in range(count):
            entry = f.read(48)
            if len(entry) != 48:
                raise ValueError(f'truncated HOG2 index: {path}')
            found.add(entry[:36].split(b'\0', 1)[0].decode('latin-1').lower())
        return found

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    args = parser.parse_args()
    names = set()
    archive_count = 0
    for d in args.directories:
        if not d.is_dir():
            print(f'WARNING: no directory: {d}', file=sys.stderr)
            continue
        for path in d.rglob('*'):
            if not path.is_file():
                continue
            if path.suffix.lower() == '.hog':
                try:
                    contents = read_hog2(path)
                except (OSError, ValueError) as e:
                    print(f'WARNING: {e}', file=sys.stderr)
                    continue
                if contents is not None:
                    archive_count += 1
                    names.update(contents)
                    print(f'HOG2: {path} ({len(contents)} files)')
            elif path.suffix.lower() in ('.ogf', '.so', '.dll'):
                names.add(path.name.lower())
    print(f'Indexed {archive_count} HOG2 archives')
    for item in HUD_NAMES:
        if item in names:
            print(f'FOUND: {item}')
        elif item.startswith('phx_') and ('ret_' + item[4:]) in names:
            print(f'OPTIONAL MISSING (standard-reticle fallback available): {item}')
        else:
            print(f'MISSING: {item}')
    print('NOTE: The Phoenix HUD can refer to optional retail-unavailable phx_*.ogf images.')
    print('      The Pyra runtime now selects existing ret_*.ogf images where available.')
    print('      This audit never generates or modifies original game data.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
