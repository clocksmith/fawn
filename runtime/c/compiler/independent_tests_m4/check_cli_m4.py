"""Independent frozen M3 CLI tests. Does not read compiler implementation."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import itertools
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
import all_fixtures as fixtures
from evaluate import Evaluator
from semantic_checks import run_vectors
from typed_oracle import Module


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def tree(root: Path) -> dict[str, list[str]]:
    result = {}
    for p in sorted(root.rglob('*')):
        name = str(p.relative_to(root))
        if p.is_symlink():
            result[name] = ['symlink', os.readlink(p)]
        elif p.is_file():
            result[name] = ['file', sha(p.read_bytes())]
        elif p.is_dir():
            result[name] = ['directory']
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    for name in ['root', 'cli', 'library', 'validator', 'out']:
        ap.add_argument('--' + name, type=Path, required=True, help='Explicit input or fresh output for this acceptance stage.')
    ap.add_argument('--corpus', action='store_true', help='Explicit input or fresh output for this acceptance stage.')
    ap.add_argument('--faults', action='store_true', help='Explicit input or fresh output for this acceptance stage.')
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('real_gate', a.root / 'bench/gates/spirv_val_gate.py')
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    assert sha((a.root/'bench/gates/spirv_val_gate.py').read_bytes()) == 'f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b'
    evaluator = Evaluator(a.library, a.out/'api', a.validator)
    records = []
    cpu_trials = 0

    def record(name: str, errors: list[str], **details: object) -> None:
        item = dict(case=name, passed=not errors, errors=errors, **details)
        records.append(item)
        if errors:
            print(json.dumps(item, sort_keys=True), flush=True)

    def invoke(parts: list[str | Path], environment: dict[str, str] | None = None,
               cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
        run = subprocess.run([str(a.cli), *map(str, parts)], capture_output=True, text=True,
            timeout=30, env=dict(os.environ, **(environment or {})), cwd=cwd or a.out)
        return run

    def reflection_expected(expect: dict) -> dict:
        bindings = []
        for original in expect['bindings']:
            item = dict(original)
            item['scalar_type'] = 'u32' if item.pop('scalar') == 1 else 'f32'
            item['access'] = 'read' if item['access'] == 1 else 'read_write'
            item['runtime_sized'] = True
            bindings.append(item)
        return dict(entry_point=expect['entry'], workgroup_size=[expect['n'], 1, 1],
                    binding_count=len(bindings), bindings=bindings)

    def verify(raw: bytes, expect: dict, path: Path) -> list[str]:
        nonlocal cpu_trials
        errors = []
        try:
            module = Module(list(struct.unpack('<'+'I'*(len(raw)//4), raw)))
            module.interface(expect)
            cpu_trials += run_vectors(module, expect)
            run = subprocess.run([str(a.validator), '--target-env', 'vulkan1.1', str(path)],
                                 capture_output=True, text=True, timeout=30)
            if run.returncode:
                errors.append('Vulkan1.1: '+run.stderr)
        except (ValueError, KeyError, IndexError, RuntimeError, AssertionError,
                OverflowError, OSError, struct.error) as exc:
            errors.append(type(exc).__name__+': '+str(exc))
        return errors

    all_cases = fixtures.admitted(a.root)
    by_source = {source: expect for _, source, expect in all_cases}
    real_paths = ['bench/inference-pipeline/kernels/residual.wgsl',
        'bench/kernels/compilation-corpus/simple_residual_add.wgsl',
        'bench/fixtures/wgsl-corpus/webgpu/sample-prefix-sum.wgsl',
        'bench/kernels/compilation-corpus/simple_scale_f32.wgsl',
        'bench/kernels/compilation-corpus/simple_relu.wgsl']
    for i, rel in enumerate(real_paths):
        source = a.root/rel
        raw_source = source.read_bytes()
        directory = a.out/f'real_consumer_{i}'
        directory.mkdir()
        op, rp, explicit = directory/'consumer.spv', directory/'explicit.json', directory/'explicit.spv'
        ok, message = gate.emit_spirv_for_wgsl(str(a.cli), source, op)
        errors = []
        if not ok:
            errors.append('real consumer: '+message)
        if set(p.name for p in directory.iterdir()) != {'consumer.spv'}:
            errors.append('single output missing or extra files')
        run = invoke(['--shader-path', source, '--out', explicit, '--reflection', rp], cwd=directory)
        if run.returncode:
            errors.append('explicit reflection: '+run.stderr)
        status, result, words, api_errors = evaluator.call(raw_source)
        errors += api_errors
        if status:
            errors.append('API rejected unchanged source')
        if op.is_file() and explicit.is_file():
            raw = op.read_bytes()
            if raw != explicit.read_bytes() or raw != struct.pack('<'+'I'*len(words), *words):
                errors.append('consumer/reflection/API bytes differ')
            errors += verify(raw, by_source[raw_source], op)
            if json.loads(rp.read_text(encoding='utf-8')) != reflection_expected(by_source[raw_source]):
                errors.append('complete reflection mismatch')
        if source.read_bytes() != raw_source:
            errors.append('source changed')
        if set(p.name for p in directory.iterdir()) != {'consumer.spv', 'explicit.spv', 'explicit.json'}:
            errors.append('temporary/default sidecar residue')
        record('real_consumer_'+str(i), errors, source=rel, source_sha256=sha(raw_source),
               output_sha256=sha(op.read_bytes()) if op.is_file() else None, message=message)
    missing = a.out/'missing_executable_output.spv'
    ok, message = gate.emit_spirv_for_wgsl(str(a.out/'absent-executable'), a.root/real_paths[0], missing)
    record('consumer_missing_executable', [] if not ok and not missing.exists() else ['unexpected success'], message=message)

    valid = (a.root/real_paths[0]).read_bytes()
    expected = by_source[valid]
    # Argument matrix: each omitted-reflection and explicit-reflection context.
    for with_reflection in [False, True]:
        mode = 'two' if with_reflection else 'one'
        names = ['no_args', 'missing_input', 'missing_output', 'input_no_value', 'output_no_value',
            'reflection_no_value', 'unknown', 'positional', 'duplicate_input', 'duplicate_output',
            'duplicate_reflection', 'duplicate_input_distinct', 'duplicate_output_distinct',
            'duplicate_reflection_distinct', 'help', 'delimiter', 'empty_input', 'empty_output', 'empty_reflection']
        for name in names:
            d = a.out/(mode+'_arg_'+name)
            d.mkdir()
            ip, op, rp = d/'input.wgsl', d/'output.spv', d/'reflection.json'
            ip.write_bytes(valid)
            parts = ['--shader-path', str(ip), '--out', str(op)]
            if with_reflection:
                parts += ['--reflection', str(rp)]
            if name == 'no_args': parts = []
            elif name == 'missing_input': parts = parts[2:]
            elif name == 'missing_output': parts = parts[:2] + parts[4:]
            elif name == 'input_no_value': parts = ['--shader-path']
            elif name == 'output_no_value': parts = ['--shader-path', str(ip), '--out']
            elif name == 'reflection_no_value': parts = ['--shader-path',str(ip),'--out',str(op),'--reflection']
            elif name == 'unknown': parts += ['--bogus', 'value']
            elif name == 'positional': parts += ['extra']
            elif name.startswith('duplicate_'):
                opt = name.split('_')[1]
                key, val = {'input':('--shader-path',ip),'output':('--out',op),'reflection':('--reflection',rp)}[opt]
                if opt == 'reflection' and not with_reflection: parts += [key,str(val)]
                parts += [key,str(d/'different') if name.endswith('distinct') else str(val)]
            elif name == 'help': parts += ['--help']
            elif name == 'delimiter': parts += ['--']
            elif name == 'empty_input': parts[1] = ''
            elif name == 'empty_output': parts[3] = ''
            elif name == 'empty_reflection':
                parts = ['--shader-path',str(ip),'--out',str(op),'--reflection','']
            before = tree(d)
            run = invoke(parts, cwd=d)
            errors = []
            # Empty strings are explicit but invalid paths; do not constrain the
            # historical path-failure diagnostic to argument-parser status 2.
            if run.returncode == 0 or (not name.startswith('empty_') and run.returncode != 2):
                errors.append('wrong argument failure status '+str(run.returncode))
            if tree(d) != before: errors.append('argument failure modified directory')
            record(mode+'_arg_'+name, errors, returncode=run.returncode, stderr=run.stderr)
        for target in ['--shader-path', '--out', '--reflection']:
            for following in ['--shader-path', '--out', '--reflection']:
                name = mode + '_option_as_value_' + target[2:] + '_' + following[2:]
                d = a.out/name
                d.mkdir()
                ip, op, rp = d/'input.wgsl', d/'output.spv', d/'reflection.json'
                ip.write_bytes(valid)
                pairs = [['--shader-path', str(ip)], ['--out', str(op)]]
                if with_reflection or target == '--reflection':
                    pairs += [['--reflection', str(rp)]]
                for pair in pairs:
                    if pair[0] == target:
                        pair[1] = following
                parts = [item for pair in pairs for item in pair]
                before = tree(d)
                run = invoke(parts, cwd=d)
                errors = []
                if run.returncode != 2:
                    errors.append('option token accepted as value: status '+str(run.returncode))
                if tree(d) != before:
                    errors.append('missing value changed directory')
                record(name, errors, returncode=run.returncode, stderr=run.stderr)
        options = [['--shader-path',None],['--out',None]] + ([['--reflection',None]] if with_reflection else [])
        for i, order in enumerate(itertools.permutations(range(len(options)))):
            d = a.out/f'{mode}_order_{i}'
            d.mkdir(); ip=d/'source.wgsl'; ip.write_bytes(valid)
            vals = [str(ip),str(d/'module.spv'),str(d/'reflection.json')]
            parts = [part for j in order for part in [options[j][0],vals[j]]]
            run = invoke(parts, cwd=d)
            wanted = {'source.wgsl','module.spv'} | ({'reflection.json'} if with_reflection else set())
            record(f'{mode}_order_{i}', [] if run.returncode==0 and set(p.name for p in d.iterdir())==wanted else ['valid option permutation failed'], returncode=run.returncode, stderr=run.stderr)

        path_names = ['missing_input','directory_input','empty_source','nul_source','oversize_source',
            'unsupported','invalid','missing_output_dir','directory_output','existing_output','output_is_input',
            'symlink_output','dangling_output','hardlink_output','long_output','long_input','source_symlink',
            'unicode_spaces','source_exact_limit','flaglike_output','escaped_option_output']
        if with_reflection:
            path_names += ['missing_reflection_dir','directory_reflection','existing_reflection','dangling_reflection',
                'symlink_reflection','reflection_is_input','same_destinations','aliased_destinations','long_reflection']
        for name in path_names:
            d = a.out/(mode+'_path_'+name); d.mkdir()
            ip, op, rp = d/'source.wgsl', d/'output.spv', d/'reflection.json'
            ip.write_bytes(valid)
            success = name in ['source_symlink','unicode_spaces','source_exact_limit','flaglike_output','escaped_option_output']
            if name == 'missing_input': ip=d/'absent.wgsl'
            elif name == 'directory_input': ip=d/'subdir'; ip.mkdir()
            elif name == 'empty_source': ip.write_bytes(b'')
            elif name == 'nul_source': ip.write_bytes(valid+b'\0')
            elif name == 'oversize_source': ip.write_bytes(valid+b' '*(65537-len(valid)))
            elif name == 'unsupported': ip.write_bytes(b'@compute @workgroup_size(1) fn main() { loop {} }')
            elif name == 'invalid': ip.write_bytes(b'not valid wgsl')
            elif name == 'missing_output_dir': op=d/'absent'/'output.spv'
            elif name == 'directory_output': op.mkdir()
            elif name == 'existing_output': op.write_bytes(b'OLD OUTPUT\0\xff')
            elif name == 'output_is_input': op=ip
            elif name == 'symlink_output': op.symlink_to(ip)
            elif name == 'dangling_output': op.symlink_to(d/'absent')
            elif name == 'hardlink_output': os.link(ip,op)
            elif name == 'long_output': op=d/('x'*5000)
            elif name == 'long_input': ip=d/('x'*5000)
            elif name == 'source_symlink':
                link=d/'linked.wgsl';link.symlink_to(ip);ip=link
            elif name == 'unicode_spaces': op=d/'out ü module.spv';rp=d/'reflection ü data.json'
            elif name == 'source_exact_limit': ip.write_bytes(valid+b' '*(65536-len(valid)))
            elif name == 'flaglike_output': op=d/'--unknown';rp=d/'--help'
            elif name == 'escaped_option_output': op=d/'--reflection';rp=d/'--out'
            elif name == 'missing_reflection_dir': rp=d/'absent'/'reflection.json'
            elif name == 'directory_reflection': rp.mkdir()
            elif name == 'existing_reflection': rp.write_bytes(b'OLD REFLECTION\0\xff')
            elif name == 'dangling_reflection': rp.symlink_to(d/'absent')
            elif name == 'symlink_reflection': rp.symlink_to(ip)
            elif name == 'reflection_is_input': rp=ip
            elif name == 'same_destinations': rp=op
            elif name == 'aliased_destinations':
                (d/'alias').symlink_to(d,target_is_directory=True);rp=d/'alias'/'output.spv'
            elif name == 'long_reflection': rp=d/('x'*5000)
            before=tree(d)
            parts=['--shader-path',str(ip),'--out',str(op)]+(['--reflection',str(rp)] if with_reflection else [])
            run=invoke(parts, cwd=d)
            after=tree(d);errors=[]
            if success:
                wanted=dict(before)
                if run.returncode: errors.append('expected success: '+run.stderr)
                for p in [op]+([rp] if with_reflection else []):
                    if p.is_file():wanted[str(p.relative_to(d))]=['file',sha(p.read_bytes())]
                    else: errors.append('missing output')
                if after!=wanted:errors.append('source altered or extra sidecar/temp')
            else:
                if not run.returncode: errors.append('failure case succeeded')
                if after!=before: errors.append('failure changed source/artifact/link or left residue')
            record(mode+'_path_'+name,errors,returncode=run.returncode,stderr=run.stderr)

    if a.corpus:
        for index,(name,source,expect) in enumerate(all_cases):
            d=a.out/f'corpus_valid_{index}';d.mkdir();ip=d/'source.wgsl';op=d/'output.spv';ip.write_bytes(source)
            ok,message=gate.emit_spirv_for_wgsl(str(a.cli),ip,op);errors=[]
            status,result,words,api_errors=evaluator.call(source);errors+=api_errors
            if not ok:errors.append('consumer: '+message)
            else:
                raw=op.read_bytes()
                if status or raw!=struct.pack('<'+'I'*len(words),*words):errors.append('API and consumer bytes differ')
                errors+=verify(raw,expect,op)
            if ip.read_bytes()!=source or set(p.name for p in d.iterdir())!={'source.wgsl','output.spv'}:errors.append('source or residue')
            record('corpus_valid_'+name,errors,source_sha256=sha(source))
        for index,(name,source,_,_) in enumerate(fixtures.rejected()):
            for existing in [False,True]:
                d=a.out/f'corpus_invalid_{index}_{existing}';d.mkdir();ip=d/'source.wgsl';op=d/'output.spv';ip.write_bytes(source)
                if existing:op.write_bytes(b'OLD')
                before=tree(d)
                ok,message=gate.emit_spirv_for_wgsl(str(a.cli),ip,op)
                record('corpus_invalid_'+name+'_'+str(existing),[] if not ok and tree(d)==before else ['bad source admitted or filesystem changed'],message=message)

    if a.faults:
        for with_reflection in [False,True]:
            mode='two' if with_reflection else 'one'
            fault_cases=[('write',1),('close',1),('close',2),('link',1),('short',1),('eintr',1)]
            if with_reflection:fault_cases += [('write',2),('close',3),('link',2),('short',2),('eintr',2)]
            for operation,count in fault_cases:
                for state in ['new','existing','symlink']:
                    d=a.out/f'{mode}_fault_{operation}_{count}_{state}';d.mkdir()
                    ip=d/'source.wgsl';ip.write_bytes(valid);op=d/'output.spv';rp=d/'reflection.json'
                    if state=='existing':op.write_bytes(b'KEEP')
                    elif state=='symlink':op.symlink_to(ip)
                    before=tree(d)
                    run=invoke(['--shader-path',str(ip),'--out',str(op)]+(['--reflection',str(rp)] if with_reflection else []),
                        dict(M3_FAULT_OPERATION=operation,M3_FAULT_CALL=str(count)), cwd=d)
                    success=operation in ['short','eintr'] and state=='new';errors=[]
                    if success:
                        if run.returncode:errors.append('retryable I/O did not complete: '+run.stderr)
                        if op.is_file():
                            status,result,words,api_errors=evaluator.call(valid);errors+=api_errors
                            if op.read_bytes()!=struct.pack('<'+'I'*len(words),*words):errors.append('retry changed bytes')
                        else:errors.append('missing output')
                        wanted=dict(before)
                        for p in [op]+([rp] if with_reflection else []):
                            if p.is_file():wanted[str(p.relative_to(d))]=['file',sha(p.read_bytes())]
                        if tree(d)!=wanted:errors.append('retry leaked files')
                    else:
                        if not run.returncode:errors.append('injected/no-clobber failure unobserved')
                        if tree(d)!=before:errors.append('fault changed preexisting/source or leaked files')
                    record(f'{mode}_fault_{operation}_{count}_{state}',errors,returncode=run.returncode,stderr=run.stderr)
    report=dict(checks=len(records),passed=sum(x['passed'] for x in records),failed=[x for x in records if not x['passed']],
                cpu_trials=cpu_trials,cli_sha256=sha(a.cli.read_bytes()),records=records)
    (a.out/'results.json').write_text(json.dumps(report,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2, sort_keys=True),flush=True)
    return int(bool(report['failed']))

if __name__=='__main__':
    raise SystemExit(main())
