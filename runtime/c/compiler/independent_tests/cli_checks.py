"""External CLI artifact and complete reflection tests."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import subprocess

from evaluate import sha
from fixtures import admitted, rejected
from semantic_checks import run_vectors
from typed_oracle import Module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    cpu_trials = 0

    def invoke(parts: list[str]) -> subprocess.CompletedProcess:
        return subprocess.run([str(args.cli.resolve()), *parts], capture_output=True,
                              text=True, timeout=15)

    for name, source, expect in admitted(args.root):
        input_path, output, reflection = [args.out / (name + suffix) for suffix in ['.wgsl', '.spv', '.json']]
        input_path.write_bytes(source)
        call = invoke(['--shader-path', str(input_path), '--out', str(output), '--reflection', str(reflection)])
        errors = []
        if call.returncode:
            errors.append('admitted CLI source rejected: ' + call.stderr)
        else:
            try:
                raw = output.read_bytes()
                if len(raw) % 4 or raw[:4] != bytes.fromhex('03022307'):
                    errors.append('output is not little-endian SPIR-V')
                module = Module(list(struct.unpack('<' + 'I' * (len(raw) // 4), raw)))
                module.interface(expect)
                cpu_trials += run_vectors(module, expect)
                obj = json.loads(reflection.read_text(encoding='utf-8'))
                desired = dict(entry_point=expect['entry'], workgroup_size=[expect['n'], 1, 1],
                               binding_count=len(expect['bindings']), bindings=[])
                for b in expect['bindings']:
                    item = dict(b)
                    item['scalar_type'] = 'u32' if item.pop('scalar') == 1 else 'f32'
                    item['access'] = 'read' if item['access'] == 1 else 'read_write'
                    item['runtime_sized'] = True
                    desired['bindings'].append(item)
                for key, value in desired.items():
                    if obj.get(key) != value:
                        errors.append(f'incomplete/wrong reflection {key}: {obj.get(key)} != {value}')
                val = subprocess.run([str(args.validator), '--target-env', 'vulkan1.1', str(output)], capture_output=True, text=True, timeout=15)
                if val.returncode:
                    errors.append('spirv-val: ' + val.stderr)
            except Exception as exc:
                errors.append(type(exc).__name__ + ': ' + str(exc))
        if input_path.read_bytes() != source:
            errors.append('CLI changed input')
        records.append(dict(case=name, passed=not errors, errors=errors,
                            source_sha256=sha(source), output_sha256=sha(output.read_bytes()) if output.exists() else None))
    valid_source = admitted(args.root)[72][1]
    for i, (name, source, _, _) in enumerate(rejected()):
        directory = args.out / ('invalid_' + str(i))
        directory.mkdir()
        ip, op, rp = directory / 'input.wgsl', directory / 'output.spv', directory / 'reflection.json'
        ip.write_bytes(source)
        # New destinations and previous artifact destinations exercise both paths.
        for existing in [False, True]:
            if existing:
                op.write_bytes(b'OLD SPIR-V\0\xff')
                rp.write_bytes(b'OLD REFLECTION\0\xff')
            before = {p: p.read_bytes() for p in [ip, op, rp] if p.exists()}
            call = invoke(['--shader-path', str(ip), '--out', str(op), '--reflection', str(rp)])
            errors = []
            if not call.returncode:
                errors.append('invalid source CLI succeeded')
            if any(p.read_bytes() != data for p, data in before.items()):
                errors.append('failure changed existing artifact')
            if not existing and (op.exists() or rp.exists()):
                errors.append('failure published new partial artifact')
            records.append(dict(case='reject_' + name + ('_existing' if existing else '_new'), passed=not errors, errors=errors, returncode=call.returncode))
    directory = args.out / 'failures'
    directory.mkdir()
    ip = directory / 'valid.wgsl'
    ip.write_bytes(valid_source)
    for name in ['missing_input', 'missing_output_dir', 'missing_reflection_dir', 'same_destinations',
                 'output_is_input', 'reflection_is_input', 'unknown_option', 'missing_reflection_arg',
                 'existing_output_only', 'existing_reflection_only', 'symlink_output', 'symlink_reflection',
                 'directory_output', 'directory_reflection']:
        sub = directory / name
        sub.mkdir()
        op, rp = sub / 'output.spv', sub / 'reflection.json'
        source_path = ip
        if name == 'missing_input':
            source_path = sub / 'absent.wgsl'
        elif name == 'missing_output_dir':
            op = sub / 'absent' / 'output.spv'
        elif name == 'missing_reflection_dir':
            rp = sub / 'absent' / 'reflection.json'
        elif name == 'same_destinations':
            rp = op
        elif name == 'output_is_input':
            op = ip
        elif name == 'reflection_is_input':
            rp = ip
        elif name == 'existing_output_only':
            op.write_bytes(b'old output')
        elif name == 'existing_reflection_only':
            rp.write_bytes(b'old reflection')
        elif name == 'symlink_output':
            op.symlink_to(ip)
        elif name == 'symlink_reflection':
            rp.symlink_to(ip)
        elif name == 'directory_output':
            op.mkdir()
        elif name == 'directory_reflection':
            rp.mkdir()
        before = {p: p.read_bytes() for p in [ip, op, rp] if p.is_file()}
        existed = {p for p in [op, rp] if p.exists()}
        parts = ['--shader-path', str(source_path), '--out', str(op), '--reflection', str(rp)]
        if name == 'unknown_option':
            parts += ['--bogus']
        if name == 'missing_reflection_arg':
            parts = parts[:-1]
        call = invoke(parts)
        errors = []
        if not call.returncode:
            errors.append('failure scenario succeeded')
        if any(p.read_bytes() != data for p, data in before.items()):
            errors.append('failure changed existing source/output/reflection')
        if any(p.exists() for p in {op, rp} - existed):
            errors.append('failure published new artifact')
        records.append(dict(case=name, passed=not errors, errors=errors, returncode=call.returncode, stderr=call.stderr))
    result = dict(checks=len(records), passed=sum(x['passed'] for x in records),
                  failed=[r for r in records if not r['passed']], cpu_trials=cpu_trials,
                  cli_sha256=sha(args.cli.read_bytes()), records=records)
    (args.out / 'cli-results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'records'}, indent=2))
    return int(bool(result['failed']))


if __name__ == '__main__':
    raise SystemExit(main())
