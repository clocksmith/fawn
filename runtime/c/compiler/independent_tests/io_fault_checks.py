"""Exercise CLI rollback after each artifact write/close/publication failure."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--interposer', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    source = args.source.read_bytes()
    for operation, count in [('write', 1), ('write', 2), ('close', 1), ('close', 2), ('close', 3), ('link', 1), ('link', 2)]:
        directory = args.out / f'{operation}_{count}'
        directory.mkdir()
        spv, reflection = directory / 'output.spv', directory / 'reflection.json'
        environment = dict(os.environ, LD_PRELOAD=str(args.interposer.resolve()),
                           DOE_TEST_FAIL_OPERATION=operation, DOE_TEST_FAIL_CALL=str(count))
        run = subprocess.run([str(args.cli.resolve()), '--shader-path', str(args.source.resolve()),
                              '--out', str(spv), '--reflection', str(reflection)],
                             env=environment, capture_output=True, text=True, timeout=15)
        errors = []
        if not run.returncode:
            errors.append('injected failure was not observed')
        if list(directory.iterdir()):
            errors.append('partial output or temporary residue remained')
        if args.source.read_bytes() != source:
            errors.append('source changed')
        records.append(dict(operation=operation, call=count, passed=not errors,
                            errors=errors, returncode=run.returncode, stderr=run.stderr))
    (args.out / 'results.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(records, indent=2))
    return int(not all(r['passed'] for r in records))


if __name__ == '__main__':
    raise SystemExit(main())
