"""Contract-derived fixtures, authored before implementation inspection."""
from __future__ import annotations

import hashlib
import itertools
import re
from pathlib import Path

import legacy_fixtures
import legacy_named_fixtures


def binding(name: str, group: int, slot: int, scalar: int = 2,
            access: int = 1) -> dict:
    return dict(name=name, group=group, binding=slot, scalar=scalar,
                access=access, element_stride=4, runtime_sized=1)


def residual(names: tuple = ('a', 'b', 'out'), order: tuple = (0, 1, 2),
             keys: tuple = ((0, 0), (0, 1), (0, 2)), scalar: int = 2,
             n: int = 256, alias: str | None = 'idx', parameter: str = 'gid',
             entry: str = 'main', reverse: bool = False, vec: str = 'vec3u',
             extra: int = 0) -> tuple[bytes, dict]:
    bindings = [binding(names[i], *keys[i], scalar, 2 if i == 2 else 1) for i in order]
    for i in range(extra):
        bindings.append(binding('unused_' + str(i), 3, 31 - i, 1 if i % 2 else 2, 1))
    declarations = []
    for b in bindings:
        attrs = [f"@group({b['group']})", f"@binding({b['binding']})"]
        if reverse:
            attrs.reverse()
        access = 'read' if b['access'] == 1 else 'read_write'
        typ = 'u32' if b['scalar'] == 1 else 'f32'
        declarations.append(' '.join(attrs) + f" var<storage, {access}> {b['name']}: array<{typ}>;")
    attrs = f'@workgroup_size({n}) @compute' if reverse else f'@compute @workgroup_size({n})'
    index = alias or parameter + '.x'
    body = (f'let {alias} = {parameter}.x; ' if alias else '')
    body += f'{names[2]}[{index}] = {names[0]}[{index}] + {names[1]}[{index}];'
    source = '\n'.join(declarations) + f'\n{attrs}\nfn {entry}(@builtin(global_invocation_id) {parameter}: {vec}) {{ {body} }}\n'
    expect = dict(entry=entry, n=n, bindings=bindings, output=keys[2],
                  operands=[keys[0], keys[1]], scalar=scalar)
    return source.encode(), expect


def convert_legacy(case: tuple) -> tuple:
    name, source, old = case
    expect = dict(entry=old['entry'], n=old['n'],
        bindings=[binding(old['storage'], 0, 0, 1, 2)], output=(0, 0),
        operands=[(0, 0), old['k']], scalar=1)
    return 'legacy_' + name, source, expect


def admitted(root: Path) -> list:
    cases = [convert_legacy(c) for c in legacy_fixtures.admitted()]
    cases += [convert_legacy(c) for c in legacy_named_fixtures.cases()[0]]
    _, exp = residual()
    for rel in ['bench/inference-pipeline/kernels/residual.wgsl',
                'bench/kernels/compilation-corpus/simple_residual_add.wgsl']:
        raw = (root / rel).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == '75ba7169ecc1af5f1ee94a94e775c3229fb833e61c0cb16af36a3fc045a31c62', rel
        cases.append(('real_' + Path(rel).stem, raw, exp))
    for i, order in enumerate(itertools.permutations(range(3))):
        src, exp = residual(names=('left_' + str(i), 'right_' + str(i), 'dst_' + str(i)),
            order=order, keys=((3, 31), (1, 17), (0, 3)), n=1 if i % 2 else 256,
            alias=None if i % 2 else 'index', parameter='position', entry='run_' + str(i),
            reverse=bool(i % 2), vec='vec3<u32>' if i % 3 else 'vec3u')
        cases.append(('permuted_resources_' + str(i), src, exp))
    for scalar in [1, 2]:
        for extra in [0, 5]:
            src, exp = residual(scalar=scalar, extra=extra)
            cases.append((f'typed_loads_{scalar}_buffers_{extra + 3}', src, exp))
    src, exp = residual()
    toks = re.findall(rb'[A-Za-z_][A-Za-z_0-9]*|[0-9]+u?|[^\s]', src)
    cases += [('residual_nested_comments', b'/*lead*/' + b'/*nested /* inner */ outer */'.join(toks) + b'//end', exp),
              ('residual_line_comments', b'//lead\n' + b'//tok\n'.join(toks), exp),
              ('residual_exact_byte_limit', src + b' ' * (65536 - len(src)), exp),
              ('residual_deep_nested_comments', b'/*' * 16000 + b'*/' * 16000 + src, exp)]
    for alias in ['x', 'u32', 'f32', 'array', 'vec3u', 'compute', 'i' * 63]:
        src, exp = residual(alias=alias)
        cases.append(('alias_' + alias[:12], src, exp))
    for parameter in ['u32', 'f32', 'array', 'vec3', 'vec3u', 'main']:
        src, exp = residual(parameter=parameter)
        cases.append(('parameter_' + parameter, src, exp))
    src, exp = residual(names=('L' * 63, 'R' * 63, 'O' * 63), alias='I' * 63,
                         parameter='P' * 63, entry='E' * 63)
    cases.append(('max_names', src, exp))
    src, exp = residual(scalar=1)
    cases.append(('u32_constant_first', b'const delta: u32 = 4294967295u;\n' +
                  src.replace(b'a[idx] + b[idx]', b'delta + b[idx]'),
                  dict(exp, operands=[4294967295, (0, 1)])))
    cases.append(('u32_literal_first', src.replace(b'a[idx] + b[idx]', b'17u + b[idx]'),
                  dict(exp, operands=[17, (0, 1)])))
    src, exp = residual()
    cases.append(('unused_constant_alias_shadow', b'const idx: u32 = 7u;\n' + src, exp))
    src, exp = residual()
    for mask in range(8):
        text = src
        for i, name in enumerate([b'out', b'a', b'b']):
            if mask & (1 << i):
                text = text.replace(name + b'[idx]', name + b'[gid.x]')
        cases.append(('mixed_alias_direct_' + str(mask), text, exp))
    for scalar in [1, 2]:
        src, exp = residual(scalar=scalar)
        src = src.replace(b'var<storage, read> a:', b'var<storage, read_write> a:')
        src = src.replace(b'out[idx] =', b'a[idx] =')
        exp = dict(exp, output=(0, 0), bindings=[dict(b, access=2) if b['name'] == 'a' else b for b in exp['bindings']])
        cases.append(('in_place_two_loads_' + str(scalar), src, exp))
    src, exp = residual()
    cases.append(('crlf_line_endings', src.replace(b'\n', b'\r\n'), exp))
    cases.append(('cr_line_endings', src.replace(b'\n', b'\r'), exp))
    return cases


def rejected() -> list:
    cases = [(n, s, off, status) for n, s, off, status in legacy_fixtures.rejected()
             if n not in {'group_nonzero', 'binding_nonzero'}]
    cases += [(n, s, None, None) for n, s in legacy_named_fixtures.cases()[1]]
    source, _ = residual()
    def change(name: str, old: bytes, new: bytes, count: int = -1) -> None:
        assert old in source, (name, old)
        cases.append((name, source.replace(old, new, count), None, None))
    for name, old, new in [
        ('f32_plus_u32', b'a[idx] + b[idx]', b'a[idx] + 1u'),
        ('f32_literal', b'a[idx] + b[idx]', b'a[idx] + 1.0'),
        ('read_only_output', b'read_write', b'read'),
        ('mixed_input_type', b'a: array<f32>', b'a: array<u32>'),
        ('mixed_output_type', b'out: array<f32>', b'out: array<u32>'),
        ('duplicate_binding_pair', b'@binding(1)', b'@binding(0)'),
        ('group_out_of_bounds', b'@group(0)', b'@group(4)'),
        ('binding_out_of_bounds', b'@binding(2)', b'@binding(32)'),
        ('binding_overflow_new', b'@binding(2)', b'@binding(4294967296)'),
        ('alias_wrong_axis', b'gid.x', b'gid.y'),
        ('alias_unknown_parameter', b'gid.x', b'other.x'),
        ('alias_explicit_type', b'let idx =', b'let idx: u32 ='),
        ('alias_second_let', b'let idx = gid.x;', b'let idx = gid.x; let j = gid.x;'),
        ('alias_duplicate_parameter', b'let idx', b'let gid'),
        ('alias_shadows_input', b'let idx = gid.x;', b'let a = gid.x;'),
        ('index_constant', b'[idx]', b'[0u]'),
        ('index_arithmetic', b'[idx]', b'[idx + 1u]'),
        ('index_missing_alias', b'let idx = gid.x;', b''),
        ('wrong_global_component', b'a[idx]', b'a[gid.y]'),
        ('extra_addition', b'a[idx] + b[idx]', b'a[idx] + b[idx] + a[idx]'),
        ('parenthesized_rhs', b'a[idx] + b[idx]', b'(a[idx] + b[idx])'),
        ('unknown_resource', b'a[idx]', b'missing[idx]'),
        ('resource_module_collision', b'b: array', b'a: array'),
        ('module_shadows_f32', b'fn main', b'fn f32'),
        ('module_shadows_array', b'fn main', b'fn array'),
        ('module_shadows_vec3u', b'fn main', b'fn vec3u'),
        ('parameter_shadows_resource', b'gid', b'a'),
        ('reserved_alias', b'idx', b'__idx'),
        ('too_long_alias', b'idx', b'i' * 64),
        ('no_load_operands', b'a[idx] + b[idx]', b'1u + 2u'),
    ]:
        change(name, old, new)
    nine, _ = residual(extra=6)
    cases.append(('ninth_buffer', nine, None, None))
    for offset in [0, 10, len(source) // 2, len(source)]:
        cases.append(('residual_nul_' + str(offset), source[:offset] + b'\0' + source[offset:], offset, None))
    cases.append(('token_limit', b'; ' * 2049, None, None))
    cases.append(('residual_byte_overflow', source + b' ' * (65537 - len(source)), None, 1))
    u32, _ = residual(scalar=1)
    cases += [('constant_alias_shadow_use', b'const idx: u32 = 7u;\n' + u32.replace(b'a[idx] + b[idx]', b'a[idx] + idx'), None, None),
              ('constant_param_shadow_use', b'const gid: u32 = 7u;\n' + u32.replace(b'a[idx] + b[idx]', b'a[idx] + gid'), None, None)]
    return cases
