#!/usr/bin/env python3
"""One fresh, bounded FCI retry with unchanged sealed input bytes/settings."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
from monitored_build import run_build

BASE = Path('/tmp/doe-instance-evaluator-0eyngbi0')
EVIDENCE = Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
WORK = BASE / 'native-fci-retry1'
RECEIPTS = EVIDENCE / 'native-fci-retry1'


def main() -> int:
    WORK.mkdir(exist_ok=False)
    RECEIPTS.mkdir(exist_ok=False)
    entries = (EVIDENCE / 'NATIVE_COMPLETE_INPUTS.sha256').read_text().splitlines()
    for line in entries:
        digest, name = line.split(maxsplit=1)
        source = BASE / 'native-controls-complete/source' / name
        assert hashlib.sha256(source.read_bytes()).hexdigest() == digest, name
        target = WORK / 'source' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    old = json.loads((EVIDENCE / 'native-controls-complete/FCI-canonical-clean-timeout.json').read_text())
    argv = old['argv'].copy()
    for flag, directory in [('--cache-dir', 'cache'),
                            ('--global-cache-dir', 'global-cache'),
                            ('--prefix', 'install')]:
        argv[argv.index(flag) + 1] = str(WORK / directory)
    environment = os.environ.copy()
    environment.update(old['environment'])
    declaration = {'source_inputs': len(entries),
                   'source_manifest': str(EVIDENCE / 'NATIVE_COMPLETE_INPUTS.sha256'),
                   'unchanged_source_bytes': True,
                   'original_process_lifecycle': 'Closed exec session, but prior child/namespace-init termination is not separately observed. No proven uncontended claim.',
                   'changed_settings': 'Only disposable cwd/source-copy/cache/global-cache/install paths; same source bytes, flags, target, optimization, toolchain, archives and explicit environment.',
                   'argv': argv}
    (RECEIPTS / 'declaration.json').write_text(json.dumps(declaration, indent=2)+'\n')
    row = run_build('FCI-canonical-fresh-retry1', argv,
                    WORK / 'source/runtime/zig', environment, RECEIPTS)
    library = WORK / 'install/lib/libwebgpu_doe.so'
    artifact = {'exists': library.exists()}
    if library.exists():
        artifact.update(size_bytes=library.stat().st_size,
                        sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                        path=str(library))
    (RECEIPTS / 'artifact.json').write_text(json.dumps(artifact, indent=2)+'\n')
    return 0 if row['returncode'] == 0 and not row['timed_out'] and library.exists() else 1


if __name__ == '__main__':
    raise SystemExit(main())
