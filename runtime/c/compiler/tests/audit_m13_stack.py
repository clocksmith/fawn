#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Audit every GCC .ci/.su frame and complete condensed library call paths.

The old sum of all unrelated functions is reported, never called a passing
path bound. Recursive SCCs and public wrappers are charged separately under
unchanged milestone ceilings. External libc/libm/OS frames remain excluded,
as in the inherited contract. Unknown internal edges and SCCs fail closed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

EXTERNAL = {
    '__errno_location', 'exp', 'expf', 'fegetenv', 'fegetround', 'feholdexcept',
    'fesetenv', 'fesetround', 'fmod', 'fmodf', 'memchr', 'memcmp', 'memcpy',
    'memmove', 'memset', 'sqrt', 'sqrtf', 'strchr', 'strlen', 'tanh', 'tanhf',
}
FAMILIES = {
    'parser_expression': (512, {'expression', 'primary', 'generic_reference', 'type_reference'}),
    'parser_control': (256, {'block', 'statement'}),
    'semantic_expression': (1536, {'integer_attribute', 'postfix_value', 'sema_builtin_value',
                                  'sema_call_value', 'sema_expression', 'sema_type_reference'}),
    'semantic_control': (768, {'lower_block', 'lower_loop'}),
    'emitter': (384, {'emit_block', 'emit_selection', 'emit_short_circuit', 'emit_loop'}),
    'uniformity_expression': (256, {'expression_bit', 'call_bit'}),
    'uniformity_control': (256, {'block_bits', 'if_bits', 'loop_bits'}),
}


def read_graph(root):
    frames = {}
    edges = {}
    for file in sorted(root.glob('*.ci')):
        for line in file.read_text().splitlines():
            if line.startswith('node:'):
                name = re.search(r'title: "([^"]+)"', line).group(1)
                match = re.search(r'\\n(\d+) bytes \(([^)]+)\)', line)
                if match:
                    value = {'bytes': int(match[1]), 'kind': match[2], 'unit': file.name}
                    assert name not in frames, ('duplicate frame identity', name)
                    assert value['kind'] in {'static', 'dynamic,bounded'}, value
                    frames[name] = value
            elif line.startswith('edge:'):
                source, target = re.search(r'sourcename: "([^"]+)" targetname: "([^"]+)"', line).groups()
                edges.setdefault(source, set()).add(target)
    assert frames
    aliases = {}
    leaves = {target for node in frames for target in edges.get(node, ()) if target not in frames}
    for target in sorted(leaves - EXTERNAL):
        assert target.endswith('.localalias'), ('unresolved internal target', target)
        file, symbol = target.rsplit(':', 1)
        canonical = symbol.removesuffix('.localalias')
        assert canonical in frames
        object_path = root / (Path(file).stem + '.o')
        symbols = {}
        for line in subprocess.check_output(['nm', str(object_path)], text=True).splitlines():
            fields = line.split()
            if len(fields) == 3:
                symbols[fields[2]] = fields[0]
        assert symbols[symbol] == symbols[canonical], ('alias address differs', symbol)
        aliases[target] = {'canonical': canonical, 'address': symbols[canonical], 'object': object_path.name}
    for targets in edges.values():
        replacements = {target: aliases[target]['canonical'] for target in targets if target in aliases}
        for target, canonical in replacements.items():
            targets.remove(target)
            targets.add(canonical)
    recorded = {(value['unit'].removesuffix('.ci'), name.rsplit(':', 1)[-1]): value['bytes']
                for name, value in frames.items()}
    usage = {}
    for file in sorted(root.glob('*.su')):
        for line in file.read_text().splitlines():
            identity, byte_count, kind = line.split('\t')
            assert kind in {'static', 'dynamic,bounded'}, (identity, kind)
            function = identity.rsplit(':', 1)[-1]
            key = (file.stem, function)
            if key not in recorded:
                # GCC .su omits the final clone ordinal printed by .ci.
                candidates = [candidate for candidate in recorded
                              if candidate[0] == file.stem
                              and re.fullmatch(re.escape(function) + r'\.\d+', candidate[1])]
                assert len(candidates) == 1, ('ambiguous GCC clone identity', key, candidates)
                key = candidates[0]
            assert key not in usage, ('duplicate stack usage frame', key)
            usage[key] = int(byte_count)
    assert usage == recorded, ('stack-usage and graph records differ', set(usage) ^ set(recorded))
    return frames, edges, aliases


def components(frames, edges):
    seen = set()
    order = []
    for start in frames:
        if start in seen:
            continue
        stack = [(start, False)]
        while stack:
            node, done = stack.pop()
            if done:
                order.append(node)
                continue
            if node in seen:
                continue
            seen.add(node)
            stack.append((node, True))
            stack.extend((target, False) for target in edges.get(node, ())
                         if target in frames and target not in seen)
    reverse = {node: set() for node in frames}
    for source, targets in edges.items():
        for target in targets:
            if source in frames and target in frames:
                reverse[target].add(source)
    groups = []
    seen = set()
    for start in reversed(order):
        if start in seen:
            continue
        group = []
        stack = [start]
        seen.add(start)
        while stack:
            node = stack.pop()
            group.append(node)
            for target in reverse[node]:
                if target not in seen:
                    seen.add(target)
                    stack.append(target)
        groups.append(group)
    return groups


def audit(root):
    frames, edges, aliases = read_graph(root)
    groups = components(frames, edges)
    index = {node: i for i, group in enumerate(groups) for node in group}
    recursive = {i for i, group in enumerate(groups)
                 if len(group) > 1 or group[0] in edges.get(group[0], ())}
    wrappers = {node for node in frames if node.startswith('doe_compiler_')}
    sccs = []
    for i in sorted(recursive):
        names = {node.rsplit(':', 1)[-1].split('.', 1)[0] for node in groups[i]}
        matching = [family for family, (_, members) in FAMILIES.items() if names <= members]
        assert len(matching) == 1, ('unknown recursive family', groups[i])
        family = matching[0]
        size = sum(frames[node]['bytes'] for node in groups[i])
        assert size <= FAMILIES[family][0], (family, size)
        sccs.append({'family': family, 'members': sorted(groups[i]), 'bytes': size,
                     'ceiling': FAMILIES[family][0]})
    depth = {}
    paths = {}

    def longest(i):
        if i in depth:
            return depth[i]
        children = {index[target] for node in groups[i] for target in edges.get(node, ())
                    if target in frames and index[target] != i}
        best = max(children, key=longest) if children else None
        weight = 0 if i in recursive else sum(frames[node]['bytes'] for node in groups[i]
                                              if node not in wrappers)
        depth[i] = weight + (depth[best] if best is not None else 0)
        paths[i] = [i] + (paths[best] if best is not None else [])
        return depth[i]

    maximum = max(range(len(groups)), key=longest)
    assert depth[maximum] <= 16384
    assert frames['doe_compiler_compile']['bytes'] <= 147968
    assert all(frames[node]['bytes'] <= 1024 for node in wrappers if node != 'doe_compiler_compile')
    assert all(value['bytes'] <= 8192 for node, value in frames.items() if node not in wrappers)
    fixed_phase = max(512 * (18 + 18 + 2) + 256 * (3 * 4 + 4),
                      1536 * (18 + 2) + 768 * (3 * 4 + 4),
                      384 * (2 * 4 + 18 + 4), 256 * (18 + 2) + 256 * (3 * 4 + 4))
    job_phase = max(512 * (64 + 64 + 2) + 256 * (3 * 32 + 4),
                    1536 * (64 + 2) + 768 * (3 * 32 + 4),
                    384 * (2 * 32 + 64 + 4), 256 * (64 + 2) + 256 * (3 * 32 + 4))
    fixed_bound = fixed_phase + depth[maximum] + frames['doe_compiler_compile']['bytes']
    job_bound = job_phase + depth[maximum] + max(frames[node]['bytes'] for node in wrappers
                                               if node != 'doe_compiler_compile')
    assert fixed_bound <= 212992 and job_bound <= 196608
    return {
        'frame_count': len(frames), 'all_frames': frames, 'sccs': sccs,
        'all_other_frame_sum': sum(value['bytes'] for node, value in frames.items()
                                   if node != 'doe_compiler_compile'),
        'wrappers': {node: frames[node]['bytes'] for node in sorted(wrappers)},
        'maximum_acyclic_library_path_bytes': depth[maximum],
        'maximum_path': [{'members': groups[i], 'separately_recursive': i in recursive}
                         for i in paths[maximum]],
        'fixed_combined': fixed_bound, 'maximum_job_combined': job_bound,
        'verified_gcc_aliases': aliases,
        'external_leaves': sorted({target for node in frames for target in edges.get(node, ())
                                   if target not in frames}),
        'ci_sha256': {file.name: hashlib.sha256(file.read_bytes()).hexdigest()
                      for file in sorted(root.glob('*.ci'))},
        'su_sha256': {file.name: hashlib.sha256(file.read_bytes()).hexdigest()
                      for file in sorted(root.glob('*.su'))},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    arguments = parser.parse_args()
    report = audit(arguments.root)
    (arguments.root / 'complete-stack.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps({key: value for key, value in report.items()
                      if key not in {'all_frames', 'ci_sha256', 'su_sha256'}}, indent=2))


if __name__ == '__main__':
    main()
