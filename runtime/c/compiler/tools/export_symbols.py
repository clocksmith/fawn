#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Convert the canonical restricted ELF export map to Mach-O symbol names.

No second public-symbol list is maintained. Reject unknown map syntax instead of
silently broadening visibility. The caller owns output staging/atomic replacement.
"""
import argparse
from pathlib import Path
import re


def symbols(text):
    stripped = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    match = re.fullmatch(r'\s*\{\s*global\s*:\s*(.*?)\s*local\s*:\s*\*\s*;\s*\}\s*;\s*',
                         stripped, flags=re.S)
    if match is None:
        raise ValueError('expected one explicit global list and local wildcard')
    names = match[1].split(';')
    if names[-1].strip():
        raise ValueError('global symbol lacks terminating semicolon')
    result = [name.strip() for name in names[:-1]]
    if not result or any(re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name) is None for name in result):
        raise ValueError('global exports must be nonempty explicit C symbol names')
    if len(result) != len(set(result)):
        raise ValueError('duplicate global symbol')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('export_map', type=Path)
    args = parser.parse_args()
    try:
        names = symbols(args.export_map.read_text())
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(''.join('_' + name + '\n' for name in names), end='')


if __name__ == '__main__':
    main()
