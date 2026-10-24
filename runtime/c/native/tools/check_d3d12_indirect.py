#!/usr/bin/env python3
"""Focused CPU source/metadata controls using an explicit existing C compiler library."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler-library', type=Path, required=True)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--output', type=Path)
    destination.add_argument('--build-root', type=Path)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    library = args.compiler_library.resolve(strict=True)
    if args.output is not None:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
    else:
        build_root = args.build_root.resolve()
        build_root.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix='indirect-layout-', dir=build_root))
    print(f'Control logs: {output}', flush=True)
    sources = ['runtime/c/common/tests/test_dispatch_guard.c',
               'runtime/c/native/tests/d3d12_indirect_layout.c',
               'runtime/c/native/src/d3d12_indirect_layout.c',
               'runtime/c/shader/src/shader.c']
    inputs = [*sources, 'runtime/c/common/include/doe_dispatch_guard.h',
              'runtime/c/native/src/d3d12_indirect_layout.h', 'runtime/c/shader/include/doe_shader.h']
    inputs += [str(path.relative_to(ROOT)) for path in (ROOT/'runtime/c/compiler/include').glob('*.h')]
    hashes = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs}
    flags = ['-std=c11','-O2','-g','-Wall','-Wextra','-Wpedantic','-Werror']
    commands = [
        [args.cc,*flags,'-I'+str(ROOT/'runtime/c/common/include'),str(ROOT/sources[0]),
         '-o',str(output/'source-control')],
        [str(output/'source-control')],
        [args.cc,*flags,'-I'+str(ROOT/'runtime/c/compiler/include'),
         '-I'+str(ROOT/'runtime/c/shader/include'),*[str(ROOT/name) for name in sources[1:]],
         str(library),'-Wl,-rpath,'+str(library.parent),'-o',str(output/'layout-control')],
        [str(output/'layout-control')]]
    receipts = []
    for index, command in enumerate(commands):
        result = subprocess.run(command,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
        (output/f'{index}.log').write_bytes(result.stdout)
        receipts.append({'command':command,'exit':result.returncode})
        print(result.stdout.decode(errors='replace'),end='',flush=True)
        if result.returncode:
            break
    unchanged = hashes == {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs}
    record = {'scope':'CPU source generation and actual compiler HLSL metadata; no SDK/device/GPU',
              'compiler_library':str(library),'compiler_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),
              'sources':hashes,'unchanged':unchanged,'commands':receipts}
    (output/'RESULTS.json').write_text(json.dumps(record,indent=2)+'\n')
    if not unchanged or any(item['exit'] for item in receipts):
        raise SystemExit(1)

if __name__ == '__main__':
    main()
