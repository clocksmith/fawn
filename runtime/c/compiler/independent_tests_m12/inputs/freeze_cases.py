"""Generate M12 independent exact inputs before any candidate execution."""
from __future__ import annotations
from collections import Counter
from datetime import datetime, timezone
from fractions import Fraction
import hashlib
import itertools
import json
from pathlib import Path
import struct
from numeric_oracle import const_results, integer_dot, runtime_bounds, self_controls
from exact_numeric_m8 import round_bits

ROOT = Path(__file__).resolve().parent
REPO = Path('/workspace/shared/doe-c-migration-20261023')
FROZEN = ROOT / 'frozen'
FROZEN.mkdir(exist_ok=True)
CASES = []
ARITY = {'min': 2, 'max': 2, 'clamp': 3, 'exp': 1, 'tanh': 1, 'sqrt': 1, 'dot': 2, 'length': 1, 'distance': 2, 'normalize': 1, 'cross': 2}
OUT = '@group(0) @binding(1) var<storage,read_write> output:array<u32>;\n'
ENTRY = '@compute @workgroup_size(1) fn main()'


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def save(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def add(name: str, source: str, status: int = 0, **metadata: object) -> None:
    source += '\n'
    path = FROZEN / (name + '.wgsl')
    if path.exists():
        raise FileExistsError(path)
    path.write_text(source, encoding='utf-8')
    CASES.append(dict(id=name, source='frozen/' + path.name, source_sha256=digest(source.encode()), expected_status=status, **metadata))


def literal(value: int, leaf: str) -> str:
    if leaf == 'AI':
        return str(value) if value != -(1 << 63) else '(-9223372036854775807-1)'
    if leaf == 'AF':
        return str(value) + '.0'
    if leaf == 'f32':
        return str(value) + '.0f'
    if leaf == 'i32':
        return str(value) + 'i' if value != -(1 << 31) else '(-2147483647i-1i)'
    return str(value) + 'u'


def vector(values: list[int], leaf: str) -> str:
    parts = [literal(x, leaf) for x in values]
    if len(parts) == 1:
        return parts[0]
    typ = f'vec{len(parts)}' + (f'<{leaf}>' if leaf in {'i32', 'u32', 'f32'} else '')
    return typ + '(' + ','.join(parts) + ')'


def outputs(width: int, leaf: str, name: str = 'result') -> str:
    lines = []
    for i in range(width):
        component = name if width == 1 else name + '.' + 'xyzw'[i]
        if leaf in {'AF', 'f32'}:
            encoded = f'bitcast<u32>(f32({component}))'
        elif leaf == 'u32':
            encoded = component
        else:
            encoded = f'bitcast<u32>(i32({component}))'
        lines.append(f'output[{i}u]={encoded};')
    return '\n'.join(lines)


def values(kind: str, leaf: str, width: int) -> list[list[int]]:
    if kind in {'min', 'max'}:
        args = [[-7, 13, -29, 41], [5, -11, 31, -43]]
        if leaf == 'u32':
            args = [[0xffffffff, 13, 0x80000000, 41], [5, 0x80000001, 31, 43]]
    elif kind == 'clamp':
        args = [[-7, 13, -29, 41], [-9, -4, -31, 0], [3, 17, -2, 39]]
        if leaf == 'u32':
            args = [[0xffffffff, 13, 0x80000000, 41], [5, 0, 0x7fffffff, 17], [99, 17, 0x80000001, 39]]
    elif kind == 'sqrt':
        args = [[1, 4, 9, 16]]
    elif kind == 'exp':
        args = [[-2, 0, 1, 3]]
    elif kind == 'tanh':
        args = [[-3, -1, 0, 2]]
    elif kind in {'length', 'normalize'}:
        args = [[3, 4, -12, 0]]
    elif kind == 'distance':
        args = [[-1, 2, 7, -4], [2, 6, -5, 1]]
    elif kind == 'cross':
        args = [[1, 2, 3], [4, 5, 6]]
    else:
        args = [[3, -5, 7, -11], [13, 17, -19, 23]]
        if leaf == 'u32':
            args = [[0xffffffff, 65536, 0x80000000, 11], [0xffffffff, 65536, 3, 23]]
    return [a[:width] for a in args]


SIGNATURES = []
for kind in ARITY:
    leaves = ['AI', 'AF', 'i32', 'u32', 'f32'] if kind in {'min', 'max', 'clamp', 'dot'} else ['AF', 'f32']
    widths = [3] if kind == 'cross' else [2, 3, 4] if kind in {'dot', 'normalize'} else [1, 2, 3, 4]
    for leaf, width in itertools.product(leaves, widths):
        SIGNATURES.append((kind, leaf, width))
        args = values(kind, leaf, width)
        rational = [[Fraction(x) for x in row] for row in args]
        expression = kind + '(' + ','.join(vector(arg, leaf) for arg in args) + ')'
        result_width = 1 if kind in {'dot', 'length', 'distance'} else width
        expected = const_results(kind, rational, leaf)
        for stage in ['module', 'local']:
            declaration = 'const result=' + expression + ';\n'
            source = OUT + (declaration if stage == 'module' else '') + ENTRY + '{\n' + (declaration if stage == 'local' else '') + outputs(result_width, leaf) + '\n}'
            add(f'{stage}_{kind}_{leaf}_{width}', source, signature=[kind, leaf, width], stage=stage, expected_options=expected, output_words=8, obligation='Full legal signature, result type, all lanes, chosen const stage graph')
        if leaf in {'i32', 'u32', 'f32'}:
            inputs = [x for row in args for x in row]
            payload = [round_bits(x) if leaf == 'f32' else x & 0xffffffff for x in inputs]
            actual_args = []
            for arg in range(ARITY[kind]):
                lanes = [f'input[{arg * width + i}u]' for i in range(width)]
                actual_args.append(lanes[0] if width == 1 else f'vec{width}<{leaf}>(' + ','.join(lanes) + ')')
            source = OUT + f'@group(0) @binding(0) var<storage,read> input:array<{leaf}>;\n' + ENTRY + '{\nlet result=' + kind + '(' + ','.join(actual_args) + ');\n' + outputs(result_width, leaf) + '\n}'
            meta = dict(signature=[kind, leaf, width], stage='runtime', input_words=payload, output_words=8, obligation='Genuine storage inputs, correct result shape and target execution')
            if leaf == 'f32':
                meta['runtime_intervals'] = [[str(x), str(y)] for x, y in runtime_bounds(kind, rational)]
            else:
                meta['expected_options'] = expected
            add(f'runtime_{kind}_{leaf}_{width}', source, **meta)
assert len(SIGNATURES) == 123
assert sum(leaf in {'i32', 'u32', 'f32'} for _, leaf, _ in SIGNATURES) == 69

# Independent exact integer stress, all permutations, explicit source-node controls.
INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1
ai_cases = [
    ('products_cancel', [1 << 32, 1 << 32], [1 << 32, -(1 << 32)]),
    ('positive_partial_cancel', [INT64_MAX, 1, -1], [1, 1, 1]),
    ('negative_partial_cancel', [INT64_MIN, -1, 1], [1, 1, 1]),
    ('final_high', [INT64_MAX, 1], [1, 1]),
    ('final_low', [INT64_MIN, -1], [1, 1]),
    ('min_exact', [INT64_MIN, 0], [1, 1]),
    ('four_large_cancel', [INT64_MIN, INT64_MIN, INT64_MAX, INT64_MAX], [INT64_MAX, -INT64_MAX, INT64_MAX, -INT64_MAX]),
    ('magnitude_2pow128', [INT64_MIN] * 4, [INT64_MIN] * 4),
    ('negative_large_cancel', [INT64_MIN, INT64_MIN, INT64_MAX, INT64_MAX], [-INT64_MAX, INT64_MAX, -INT64_MAX, INT64_MAX]),
]
for name, a, b in ai_cases:
    for index, permutation in enumerate(itertools.permutations(range(len(a)))):
        left, right = [a[i] for i in permutation], [b[i] for i in permutation]
        expression = 'dot(' + vector(left, 'AI') + ',' + vector(right, 'AI') + ')'
        try:
            result = integer_dot(left, right, 'AI')
        except OverflowError:
            source = OUT + 'const result=' + expression + ';\n' + ENTRY + '{}'
            add(f'ai_{name}_{index}', source, 4, interpretation='N1 selected, not universal upstream convergence', obligation='Exact final signed64 range, including 2^128 positive accumulation')
        else:
            source = OUT + 'const result=' + expression + ';\n' + ENTRY + '{if(result==' + literal(result, 'AI') + '){output[0]=1u;}else{output[0]=2u;}}'
            add(f'ai_{name}_{index}', source, expected_options=[[1]], output_words=8, mathematical_result=str(result), interpretation='N1 selected; contrary Tint checked-product behavior remains disclosed', obligation='Exact signed cancellation and lane permutation invariance')
for name, expression in [('argument_product', 'dot(vec2(4294967296*4294967296,0),vec2(1,1))'), ('argument_addition', 'dot(vec2(9223372036854775807+1,0),vec2(0,0))')]:
    add('ai_explicit_' + name, OUT + 'const result=' + expression + ';' + ENTRY + '{}', 4, obligation='N1 cannot erase explicit source expression overflow')
add('ai_default_i32_range', OUT + ENTRY + '{let result=dot(vec2(2147483648,1),vec2(1,1));output[0]=u32(result);}', 4, obligation='Runtime default materialization cannot wrap an out-of-range AI result')
add('ai_explicit_f32_conversion', OUT + 'const result=dot(vec2(2147483648,1),vec2(1,1));' + ENTRY + '{output[0]=bitcast<u32>(f32(result));}', expected_options=[[round_bits(2147483649)]], output_words=8, obligation='Explicit conversion follows unchanged numeric conversion owner')

# Signature errors are exercised even in non-emitted source.
for kind in ARITY:
    canonical = vector(values(kind, 'f32', 3)[0], 'f32')
    for arity in range(5):
        if arity == ARITY[kind]:
            continue
        expr = kind + '(' + ','.join([canonical] * arity) + ')'
        add(f'bad_arity_{kind}_{arity}', ENTRY + '{let result=' + expr + ';}', 4, obligation='Arity checked before bounded operand access')
    for context in ['live', 'dead', 'helper', 'skip']:
        bad = kind + '(' + ','.join(['true'] * ARITY[kind]) + ')'
        body = 'let result=' + bad + ';'
        source = ENTRY + '{' + (('return;' if context == 'dead' else '') + body) + '}'
        if context == 'helper':
            source = 'fn unused(){' + body + '}' + ENTRY + '{}'
        if context == 'skip':
            source = ENTRY + '{let result=false && (' + bad + '==true);}'
        add(f'bad_bool_{kind}_{context}', source, 4, obligation='No invalid overload escapes dead/unused/skipped validation')
    if ARITY[kind] > 1:
        add('bad_splat_' + kind, ENTRY + '{let result=' + kind + '(' + ','.join([canonical] + ['1.0f'] * (ARITY[kind] - 1)) + ');}', 4, obligation='No scalar-vector implicit splat')
        add('bad_width_' + kind, ENTRY + '{let result=' + kind + '(' + ','.join([canonical] + ['vec2f(1.0f,2.0f)'] * (ARITY[kind] - 1)) + ');}', 4, obligation='No implicit vector width conversion')
    add('bad_statement_' + kind, ENTRY + '{' + kind + '(' + ','.join([canonical] * ARITY[kind]) + ');}', 4, obligation='Builtin result requires a use')
    add('bad_generic_' + kind, ENTRY + '{let result=' + kind + '<f32>(' + ','.join([canonical] * ARITY[kind]) + ');}', 4, obligation='Explicit builtin generic arguments are semantic call errors')
    if kind not in {'min', 'max', 'clamp', 'dot'}:
        for leaf in ['i32', 'u32']:
            add(f'bad_concrete_integer_{kind}_{leaf}', ENTRY + '{let result=' + kind + '(' + ','.join([vector([1, 2, 3], leaf)] * ARITY[kind]) + ');}', 4, obligation='No implicit concrete integer-to-float builtin conversion')
for kind in ['dot', 'normalize']:
    add('bad_scalar_' + kind, ENTRY + '{let result=' + kind + '(' + ','.join(['1.0f'] * ARITY[kind]) + ');}', 4, obligation='Vector-only builtin')
for width in [2, 4]:
    add(f'bad_cross_width_{width}', ENTRY + '{let result=cross(' + ','.join([vector(list(range(1, width + 1)), 'f32')] * 2) + ');}', 4, obligation='Cross is vec3-only')

# All names resolve normally rather than dispatching by spelling.
for kind in ARITY:
    add('shadow_function_' + kind, OUT + f'fn {kind}(x:u32)->u32{{return x+5u;}}' + ENTRY + '{output[0]=' + kind + '(7u);}', expected_options=[[12]], output_words=8, obligation='User function shadows predeclared spelling')
    add('shadow_struct_' + kind, OUT + f'struct {kind}{{x:u32,y:u32}};' + ENTRY + '{let result=' + kind + '(9u,4u);output[0]=result.x;output[1]=result.y;}', expected_options=[[9], [4]], output_words=8, obligation='Nominal constructor shadows predeclared spelling')
    for place in ['local', 'module', 'formal']:
        prefix = 'const ' + kind + '=3u;' if place == 'module' else ''
        body = ('let ' + kind + '=3u;' if place == 'local' else '') + 'let result=' + kind + '(1u);'
        source = prefix + ENTRY + '{' + body + '}'
        if place == 'formal':
            source = 'fn helper(' + kind + ':u32){' + body + '}' + ENTRY + '{}'
        add(f'shadow_value_{kind}_{place}', source, 4, obligation='A resolved value is not callable; no builtin fallback')

# Mandatory bounds, reversed runtime composition, and skipped side-effect control.
for leaf in ['i32', 'u32', 'f32']:
    for width in [2, 3, 4]:
        for bad_lane in range(width):
            low = [1] * width
            high = [3] * width
            low[bad_lane] = 5
            expr = 'clamp(' + vector([2] * width, leaf) + ',' + vector(low, leaf) + ',' + vector(high, leaf) + ')'
            for context in ['local', 'skip', 'dead']:
                body = 'const result=' + expr + ';' if context == 'local' else ('return;let result=' + expr + ';' if context == 'dead' else 'let result=false && (' + expr + '.x==' + literal(2, leaf) + ');')
                add(f'bad_clamp_bounds_{leaf}_{width}_{bad_lane}_{context}', ENTRY + '{' + body + '}', 4, obligation='Mandatory per-lane const bounds survive skipped/dead evaluation')
        args = [[2, 8, 17, 29][:width], [9, 4, 23, 7][:width], [3, 6, 11, 5][:width]]
        prefix = ''.join('let a' + str(i) + '=' + vector(arg, leaf) + ';' for i, arg in enumerate(args))
        expr = 'clamp(a0,a1,a2)'
        add(f'runtime_reversed_clamp_{leaf}_{width}', OUT + ENTRY + '{' + prefix + 'let result=' + expr + ';' + outputs(width, leaf) + '}', expected_options=const_results('clamp', [[Fraction(x) for x in row] for row in args], leaf), output_words=8, stage='runtime', forbidden_glsl=[43, 44, 45], obligation='Runtime reversed bounds lower through safe composition')

# Every argument position contributes to source uniformity, even numerically zeroed.
for kind in ARITY:
    for position in range(ARITY[kind]):
        width = 3
        args = [vector(arg, 'f32') for arg in values(kind, 'f32', width)]
        args[position] = 'vec3f(f32(li),0.0f,0.0f)'
        if kind == 'dot':
            args[1 - position] = 'vec3f(0.0f)'
        expr = kind + '(' + ','.join(args) + ')'
        scalar = expr if kind in {'dot', 'length', 'distance'} else expr + '.x'
        entry = '@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32)'
        add(f'uniformity_varying_{kind}_{position}', entry + '{if(' + scalar + '==0.0f){workgroupBarrier();}}', 4, obligation='Source dependency union, not numeric simplification')
        add(f'uniformity_skipped_{kind}_{position}', entry + '{if(false && (' + scalar + '==0.0f)){workgroupBarrier();}}', 4, obligation='Skipped source argument retains uniformity dependency')
    args = [vector(arg, 'f32') for arg in values(kind, 'f32', 3)]
    expr = kind + '(' + ','.join(args) + ')'
    scalar = expr if kind in {'dot', 'length', 'distance'} else expr + '.x'
    add('uniformity_control_' + kind, '@compute @workgroup_size(4) fn main(){if(' + scalar + '==0.0f){workgroupBarrier();}}', obligation='Uniform source positive prevents always-reject')

# Effects use unique source-order history and mutable storage snapshots.
for kind in ARITY:
    width = 3
    args = values(kind, 'f32', width)
    prefix = OUT + '@group(0) @binding(0) var<storage,read_write> state:array<u32>;\n'
    for index, arg in enumerate(args):
        prefix += f'fn arg{index}()->vec3f{{state[0]=state[0]*10u+{index+1}u;state[{index+1}u]=state[{index+1}u]+1u;return ' + vector(arg, 'f32') + ';}\n'
    result_width = 1 if kind in {'dot', 'length', 'distance'} else width
    source = prefix + ENTRY + '{let result=' + kind + '(' + ','.join(f'arg{i}()' for i in range(len(args))) + ');' + outputs(result_width, 'f32') + '}'
    expected = const_results(kind, [[Fraction(x) for x in arg] for arg in args], 'f32')
    add('effects_' + kind, source, expected_options=expected, output_words=8, input_words=[0] * 8, expected_input_words=[int(''.join(str(i+1) for i in range(len(args))))] + [1] * len(args) + [0] * (7-len(args)), obligation='Each source argument executes once in order; full sentinel buffer checked')

# Explicit const finite-domain failures, distinct from runtime latitude.
for name, expression in [
    ('sqrt_negative_lane', 'sqrt(vec3f(4.0f,-1.0f,9.0f))'),
    ('normalize_zero', 'normalize(vec4f(0.0f))'),
    ('exp_overflow_lane', 'exp(vec2f(1.0f,100.0f))'),
    ('dot_f32_intermediate_overflow', 'dot(vec2f(0x1p100f,0x1p100f),vec2f(0x1p100f,-0x1p100f))'),
    ('length_f32_intermediate_overflow', 'length(vec2f(0x1p100f,0.0f))'),
    ('distance_f32_intermediate_overflow', 'distance(vec2f(0x1p127f,0.0f),vec2f(-0x1p127f,0.0f))')]:
    add('bad_domain_' + name, ENTRY + '{const result=' + expression + ';}', 4, obligation='Chosen const graph checks every evaluated nonfinite primitive')


# Stage, common-rank, aggregate projection, forward dependency and implicit conversion.
for kind in ARITY:
    width = 3
    args = values(kind, 'f32', width)
    result_width = 1 if kind in {'dot', 'length', 'distance'} else width
    expected = const_results(kind, [[Fraction(x) for x in row] for row in args], 'f32')
    literal_args = [vector(x, 'f32') for x in args]
    expression = kind + '(' + ','.join(literal_args) + ')'
    result_type = 'f32' if result_width == 1 else 'vec3f'
    add('stage_helper_' + kind, OUT + 'fn helper()->' + result_type + '{return ' + expression + ';}' + ENTRY + '{let result=helper();' + outputs(result_width, 'f32') + '}', expected_options=expected, output_words=8, obligation='Builtin helper result uses common typed lowering')
    prefix = OUT + 'const result=' + kind + '(' + ','.join('a' + str(i) for i in range(len(args))) + ');' + ''.join('const a' + str(i) + '=' + x + ';' for i, x in enumerate(literal_args))
    add('stage_forward_' + kind, prefix + ENTRY + '{' + outputs(result_width, 'f32') + '}', expected_options=expected, output_words=8, obligation='Module forward dependency resolves same fold owner')
    prefix = 'struct Box{x:vec3f};'
    body = ''.join('let a' + str(i) + '=Box(' + x + ');' for i, x in enumerate(literal_args))
    expression = kind + '(' + ','.join('a' + str(i) + '.x' for i in range(len(args))) + ')'
    add('stage_member_' + kind, OUT + prefix + ENTRY + '{' + body + 'let result=' + expression + ';' + outputs(result_width, 'f32') + '}', expected_options=expected, output_words=8, obligation='Vector member arguments are captured values')
    body = ''.join('let a' + str(i) + '=array<vec3f,1>(' + x + ');' for i, x in enumerate(literal_args))
    expression = kind + '(' + ','.join('a' + str(i) + '[0]' for i in range(len(args))) + ')'
    add('stage_array_' + kind, OUT + ENTRY + '{' + body + 'let result=' + expression + ';' + outputs(result_width, 'f32') + '}', expected_options=expected, output_words=8, obligation='Array element projection does not replay arguments')
    expression = kind + '(' + ','.join(vector(list(reversed(arg)), 'f32') + '.zyx' for arg in args) + ')'
    add('stage_swizzle_' + kind, OUT + ENTRY + '{let result=' + expression + ';' + outputs(result_width, 'f32') + '}', expected_options=expected, output_words=8, obligation='Swizzle lanes and result order survive builtin lowering')
    for shape, argument in [('array', 'array<f32,2>(1.0f,2.0f)'), ('struct', 'Box(vec3f(1.0f))')]:
        expression = kind + '(' + ','.join([argument] * ARITY[kind]) + ')'
        add('bad_shape_' + kind + '_' + shape, prefix + ENTRY + '{let result=' + expression + ';}', 4, obligation='No aggregate overload outside scalar/vector')
    if kind not in {'min', 'max', 'clamp', 'dot'}:
        abstract_args = [vector(arg, 'AI') for arg in args]
        expression = kind + '(' + ','.join(abstract_args) + ')'
        expected_ai = const_results(kind, [[Fraction(x) for x in row] for row in args], 'AF')
        add('rank_AI_to_AF_' + kind, OUT + 'const result=' + expression + ';' + ENTRY + '{' + outputs(result_width, 'AF') + '}', expected_options=expected_ai, output_words=8, obligation='Floating-only families rank AbstractInt to AbstractFloat')
for kind in ['min', 'max', 'clamp', 'dot']:
    for leaf in ['AF', 'i32', 'u32', 'f32']:
        args = [[1, 2, 3], [4, 5, 6], [7, 8, 9]][:ARITY[kind]]
        expression = kind + '(' + ','.join(vector(arg, 'AI' if i == 0 else leaf) for i, arg in enumerate(args)) + ')'
        expected = const_results(kind, [[Fraction(x) for x in row] for row in args], leaf)
        result_width = 1 if kind == 'dot' else 3
        add('rank_AI_' + leaf + '_' + kind, OUT + 'const result=' + expression + ';' + ENTRY + '{' + outputs(result_width, leaf) + '}', expected_options=expected, output_words=8, obligation='Selected same-shape common overload then representability')
    for a, b in [('i32', 'u32'), ('u32', 'f32'), ('i32', 'f32')]:
        arguments = [vector([1, 2, 3], a)] + [vector([4, 5, 6], b)] * (ARITY[kind]-1)
        add('bad_mixed_' + kind + '_' + a + '_' + b, ENTRY + '{let result=' + kind + '(' + ','.join(arguments) + ');}', 4, obligation='No mixed concrete overload')

for leaf in ['i32', 'u32']:
    for width in [2, 3, 4]:
        seeds = [([65536] * width, [65536] * width), ([0x7fffffff] * width, [1] * width)]
        seeds += [([-(1 << 31)] * width, [-(1 << 31)] * width)] if leaf == 'i32' else [([0xffffffff] * width, [0xffffffff] * width)]
        for index, (a, b) in enumerate(seeds):
            expected = [[integer_dot(a, b, leaf) & 0xffffffff]]
            for stage in ['const', 'runtime']:
                prefix = ('const ' if stage == 'const' else 'let ')
                body = prefix + 'a=' + vector(a, leaf) + ';' + prefix + 'b=' + vector(b, leaf) + ';' + prefix + 'result=dot(a,b);'
                add(f'dot_modular_{leaf}_{width}_{index}_{stage}', OUT + ENTRY + '{' + body + outputs(1, leaf) + '}', expected_options=expected, output_words=8, obligation='Products and additions are mathematical modulo32 in both stages')

for leaf in ['AF', 'f32']:
    args = [[16777216, 1, -16777216], [1, 1, 1]]
    expression = 'dot(' + ','.join(vector(x, leaf) for x in args) + ')'
    add('const_width_graph_' + leaf, OUT + 'const result=' + expression + ';' + ENTRY + '{' + outputs(1, leaf) + '}', expected_options=const_results('dot', [[Fraction(x) for x in row] for row in args], leaf), output_words=8, obligation='Per-primitive f32 versus binary64 rounding, no destination back-propagation')

for leaf in ['i32', 'u32', 'f32']:
    args = [vector([2, 3, 4], leaf), vector([1, 9, 1], leaf), vector([5, 5, 5], leaf)]
    expression = 'clamp(runtime_value,' + args[1] + ',' + args[2] + ')'
    for stage in ['plain', 'skipped', 'module_bounds', 'local_bounds']:
        prefix = OUT
        body = 'let runtime_value=' + args[0] + ';'
        used = expression
        if stage == 'module_bounds':
            prefix += 'const lower=' + args[1] + ';const upper=' + args[2] + ';'
            used = 'clamp(runtime_value,lower,upper)'
        elif stage == 'local_bounds':
            body += 'const lower=' + args[1] + ';const upper=' + args[2] + ';'
            used = 'clamp(runtime_value,lower,upper)'
        body += ('let result=false && (' + used + '.x==' + literal(0, leaf) + ');') if stage == 'skipped' else 'let result=' + used + ';'
        add('bad_clamp_runtime_value_' + leaf + '_' + stage, prefix + ENTRY + '{' + body + '}', 4, obligation='Both const bounds mandate validation even with a runtime value')

# A later argument mutates the backing resource of an earlier vector argument.
for kind in ['min', 'max', 'clamp', 'dot', 'distance', 'cross']:
    args = values(kind, 'f32', 3)
    prefix = OUT + '@group(0) @binding(0) var<storage,read_write> input:array<f32>;' + 'fn mutate()->vec3f{input[0]=91.0f;input[1]=92.0f;input[2]=93.0f;return ' + vector(args[1], 'f32') + ';}'
    arguments = ['vec3f(input[0],input[1],input[2])', 'mutate()']
    if kind == 'clamp':
        arguments.append(vector(args[2], 'f32'))
    expression = kind + '(' + ','.join(arguments) + ')'
    result_width = 1 if kind in {'dot', 'distance'} else 3
    add('snapshot_before_effect_' + kind, prefix + ENTRY + '{let result=' + expression + ';' + outputs(result_width, 'f32') + '}', input_words=[round_bits(x) for x in args[0]] + [0] * 5, expected_input_words=[round_bits(x) for x in [91, 92, 93]] + [0] * 5, expected_options=const_results(kind, [[Fraction(x) for x in row] for row in args], 'f32'), output_words=8, obligation='Captured first argument cannot reload after later source effect')

add('skipped_runtime_effects', OUT + '@group(0) @binding(0) var<storage,read_write> state:array<u32>;fn effect()->vec2f{state[0]=state[0]+1u;return vec2f(2.0f,3.0f);}' + ENTRY + '{let result=false && (dot(effect(),effect())>0.0f);output[0]=u32(result);}', input_words=[11, 22, 33, 44], expected_input_words=[11, 22, 33, 44], expected_options=[[0]], output_words=8, obligation='Skipped RHS emits no argument effect')
add('robust_oob_dot', OUT + '@group(0) @binding(0) var<storage,read> input:array<vec3u>;' + ENTRY + '{let result=dot(input[99u],vec3u(7u,11u,13u));output[0]=result;}', input_words=[9, 10, 11, 0xabcddcba], expected_options=[[0]], output_words=8, obligation='OOB vector argument is typed zero with vec3 padding untouched')
add('robust_inner_oob_outer_valid', OUT + '@group(0) @binding(0) var<storage,read> input:array<vec3u>;' + ENTRY + '{let result=dot(input[input[99u].x],vec3u(7u,11u,13u));output[0]=result;}', input_words=[9, 10, 11, 0xabcddcba], expected_options=[[9*7+10*11+11*13]], output_words=8, obligation='Inner OOB zero remains valid outer index; independent guards')

manifest = dict(schema=1, created_at=datetime.now(timezone.utc).isoformat(), case_count=len(CASES), signature_count=123, runtime_signature_count=69, stage_counts=dict(Counter(x.get('stage', 'supplement') for x in CASES)), cases=CASES)
save(ROOT / 'cases.json', manifest)
save(ROOT / 'ORACLE_CONTROLS.json', self_controls())
