"""Independent M8 scalar SPIR-V instruction interpreter, adapted from MIT M7.

Extends the sealed M6 instruction interpreter, never reads WGSL source. Values
are selected only by emitted instruction IDs, decorated bindings and actual
branch predecessor labels. A harness step bound is unfinished, not termination.
"""
from __future__ import annotations
import math
import struct
from exact_numeric_m8 import finite_value, fp_operation, round_bits, signed, trunc_div
from rational_oracle import operation, exact_value
import spirv_oracle_base as base
from compute_oracle_m6 import Module as PreviousModule, BindingAdmissionError
from typed_oracle import f32_bits, bits_f32
from spirv_oracle_base import ExecutionError, OracleLimit, Pointer, StructuralError

base.NAMES.update({246: 'LoopMerge', 180: 'FOrdEqual', 181: 'FUnordEqual',
    182: 'FOrdNotEqual', 183: 'FUnordNotEqual', 185: 'FUnordLessThan',
    187: 'FUnordGreaterThan', 189: 'FUnordLessThanEqual',
    191: 'FUnordGreaterThanEqual'})
NUMERIC_OPS = {109:'ConvertFToU',110:'ConvertFToS',111:'ConvertSToF',112:'ConvertUToF',
    114:'SConvert',115:'FConvert',126:'SNegate',127:'FNegate',134:'UDiv',135:'SDiv',
    136:'FDiv',137:'UMod',138:'SRem',139:'SMod',140:'FRem',141:'FMod',156:'IsNan',
    157:'IsInf',158:'IsFinite',159:'IsNormal',160:'SignBitSet',161:'LessOrGreater',
    162:'Ordered',163:'Unordered',173:'SGreaterThan',175:'SGreaterThanEqual',
    177:'SLessThan',179:'SLessThanEqual',194:'ShiftRightLogical',
    195:'ShiftRightArithmetic',196:'ShiftLeftLogical',197:'BitwiseOr',198:'BitwiseXor',
    199:'BitwiseAnd',200:'Not'}
base.NAMES.update(NUMERIC_OPS)
base.RESULT_SECOND.update(NUMERIC_OPS)
base.MIN_ARGS.update({op: 3 if op in {109,110,111,112,114,115,126,127,156,157,158,159,160,200}
                     else 4 for op in NUMERIC_OPS})
base.RESULT_SECOND.update(range(180,192))
base.MIN_ARGS.update({246:3, **{op:4 for op in range(180,192)}})

class StepLimit(ExecutionError):
    """Execution is unfinished at a harness-only instruction limit."""

class Module(PreviousModule):
    def __init__(self, words: list[int]) -> None:
        base.Module.__init__(self, words)
        for op, args, _ in self.instructions:
            if op == 22:
                self.types[args[0]] = (op, args[1:])
            elif op == 46:
                typ = self.types[args[0]]
                if typ not in {(20, ()), (21, (32, 0)), (21, (32, 1)), (22, (32,))}:
                    raise OracleLimit('non-scalar OpConstantNull')
                self.constants[args[1]] = False if typ == (20, ()) else 0
        self.last_steps = 0
        self.last_opcodes = {}
        self.last_local_accesses = []

    def execute(self, buffers: dict[tuple[int, int], list[int]], builtins: dict[int, object],
                read_only: set[tuple[int, int]],
                uniforms: dict[tuple[int, int], bytes] | None = None,
                step_limit: int = 1000000) -> list:
        self.last_steps = 0
        self.last_opcodes = {}
        self.last_local_accesses = []
        uniforms = {} if uniforms is None else uniforms
        # Admission is independent of reflection and is before any instruction.
        for key, minimum in self.uniform_minimums.items():
            if key not in uniforms or len(uniforms[key]) < minimum:
                raise BindingAdmissionError(f'short uniform {key}: requires {minimum}')
        env = dict(self.constants)
        memory = {}
        accesses = []
        for rid, (_, storage, init) in self.variables.items():
            env[rid] = Pointer(rid)
            if storage == 7:
                memory[rid] = env.get(init[0]) if init else None

        def storage_target(ptr: Pointer) -> tuple[tuple[int, int], int]:
            if len(ptr.indices) != 2 or ptr.indices[0] != 0:
                raise OracleLimit(f'storage pointer path {ptr.indices}')
            key, index = self.resources[ptr.root], ptr.indices[1]
            if not 0 <= index < len(buffers[key]):
                raise ExecutionError(f'OOB resource access descriptor={key} index={index} length={len(buffers[key])}')
            return key, index

        def load(ptr: Pointer):
            if not isinstance(ptr, Pointer):
                raise ExecutionError('load of non-pointer')
            if ptr.root in self.inputs:
                obj = builtins[self.inputs[ptr.root]]
                for index in ptr.indices:
                    obj = obj[index]
                return obj
            if ptr.root in self.uniforms:
                if len(ptr.indices) != 1:
                    raise ExecutionError('uniform access must select one fixed member')
                key, struct_id = self.uniforms[ptr.root]
                member = ptr.indices[0]
                members = self.types[struct_id][1]
                if not 0 <= member < len(members):
                    raise ExecutionError('uniform member outside emitted type')
                offset = self.member_decorations[(struct_id, member)][35][0]
                accesses.append(('uniform_read', key, offset))
                return struct.unpack_from('<I', uniforms[key], offset)[0]
            if ptr.root in self.resources:
                key, index = storage_target(ptr)
                accesses.append(('read', key, index))
                return buffers[key][index]
            obj = memory[ptr.root]
            if obj is None:
                raise ExecutionError("load of uninitialized Function memory")
            self.last_local_accesses.append(("read", ptr.root, obj))
            for index in ptr.indices:
                obj = obj[index]
            return obj

        def store(ptr: Pointer, value: int) -> None:
            if not isinstance(ptr, Pointer):
                raise ExecutionError('store to non-pointer')
            if ptr.root in self.uniforms:
                raise ExecutionError('write to uniform')
            if ptr.root in self.resources:
                key, index = storage_target(ptr)
                accesses.append(('write', key, index))
                if key in read_only:
                    raise ExecutionError(f'write to read-only descriptor {key}')
                buffers[key][index] = value & 0xffffffff
            elif ptr.indices:
                raise OracleLimit('local aggregate stores')
            else:
                memory[ptr.root] = value
                self.last_local_accesses.append(("write", ptr.root, value))

        def run(fn: int, arguments: tuple = (), depth: int = 0):
            if depth > 10:
                raise ExecutionError('function recursion budget exceeded')
            body = self.functions[fn]
            labels = {a[0]: i for i, (op, a, _) in enumerate(body) if op == 248}
            pc, steps, parameter_index = 0, 0, 0
            block = previous = None
            while pc < len(body):
                steps += 1
                self.last_steps += 1
                if self.last_steps > step_limit:
                    raise StepLimit('unfinished at harness instruction bound')
                op, a, _ = body[pc]
                pc += 1
                self.last_opcodes[op] = self.last_opcodes.get(op, 0) + 1
                if op == 55:
                    env[a[1]] = arguments[parameter_index]
                    parameter_index += 1
                elif op == 57:
                    env[a[1]] = run(a[2], tuple(env[i] for i in a[3:]), depth + 1)
                elif op == 248:
                    block = a[0]
                    incoming_values = {}
                    while pc < len(body) and body[pc][0] == 245:
                        _, phi, _ = body[pc]
                        incoming = [phi[i] for i in range(2, len(phi), 2) if phi[i + 1] == previous]
                        if len(incoming) != 1:
                            raise ExecutionError('Phi predecessor missing')
                        incoming_values[phi[1]] = env[incoming[0]]
                        pc += 1
                    env.update(incoming_values)
                elif op in {0, 8, 246, 247}:
                    pass
                elif op == 59:
                    env[a[1]] = Pointer(a[1])
                    memory[a[1]] = env.get(a[3]) if len(a) > 3 else None
                elif op == 61:
                    env[a[1]] = load(env[a[2]])
                elif op == 62:
                    store(env[a[0]], env[a[1]])
                elif op in {65, 66}:
                    ptr = env[a[2]]
                    if not isinstance(ptr, Pointer):
                        raise ExecutionError('AccessChain base is not pointer')
                    env[a[1]] = Pointer(ptr.root, ptr.indices + tuple(env[i] for i in a[3:]))
                    if ptr.root in self.resources:
                        storage_target(env[a[1]])
                elif op == 68:
                    ptr = env[a[2]]
                    if not isinstance(ptr, Pointer) or ptr.root not in self.resources or ptr.indices or a[3] != 0:
                        raise OracleLimit('ArrayLength target is not a resource runtime array')
                    env[a[1]] = len(buffers[self.resources[ptr.root]])
                elif op == 81:
                    obj = env[a[2]]
                    for index in a[3:]:
                        obj = obj[index]
                    env[a[1]] = obj
                elif op == 80:
                    env[a[1]] = tuple(env[i] for i in a[2:])
                elif op in {83, 113, 114, 115, 124}:
                    env[a[1]] = env[a[2]]
                elif op in {109, 110}:
                    bits = env[a[2]]
                    if (bits & 0x7f800000) == 0x7f800000:
                        raise ExecutionError('unsafe nonfinite float-to-int operand before select')
                    value = finite_value(bits)
                    lo, hi = (0, 0xffffffff) if op == 109 else (-0x80000000, 0x7fffffff)
                    converted = trunc_div(value.numerator, value.denominator)
                    if not lo <= converted <= hi:
                        raise ExecutionError('unsafe out-of-range float-to-int operand before select')
                    env[a[1]] = converted & 0xffffffff
                elif op in {111, 112}:
                    value = signed(env[a[2]]) if op == 111 else env[a[2]]
                    env[a[1]] = round_bits(value)
                elif op == 126:
                    env[a[1]] = (-env[a[2]]) & 0xffffffff
                elif op == 127:
                    env[a[1]] = env[a[2]] ^ 0x80000000
                elif op in {134, 135, 137, 138, 139}:
                    left, right = env[a[2]], env[a[3]]
                    if op in {135, 138, 139}:
                        left, right = signed(left), signed(right)
                    if right == 0 or (op in {135,138,139} and left == -0x80000000 and right == -1):
                        raise ExecutionError('unsafe integer division/remainder operand before select')
                    quotient = trunc_div(left, right)
                    value = quotient if op in {134,135} else left - quotient * right
                    if op == 139:
                        value = left % right
                    env[a[1]] = value & 0xffffffff
                elif op in {194, 195, 196}:
                    left, right = env[a[2]], env[a[3]]
                    if not 0 <= right < 32:
                        raise ExecutionError('unsafe unmasked shift count before select')
                    if op == 194:
                        value = left >> right
                    elif op == 195:
                        value = signed(left) >> right
                    else:
                        value = left << right
                    env[a[1]] = value & 0xffffffff
                elif op in {197, 198, 199, 200}:
                    left = env[a[2]]
                    right = env[a[3]] if len(a) > 3 else 0
                    env[a[1]] = {197: lambda: left | right, 198: lambda: left ^ right,
                                 199: lambda: left & right, 200: lambda: ~left}[op]() & 0xffffffff
                elif op in {173,175,177,179}:
                    left, right = signed(env[a[2]]), signed(env[a[3]])
                    env[a[1]] = {173: lambda: left > right, 175: lambda: left >= right,
                                 177: lambda: left < right, 179: lambda: left <= right}[op]()
                elif 156 <= op <= 163:
                    left = bits_f32(env[a[2]])
                    right = bits_f32(env[a[3]]) if len(a) > 3 else 0.0
                    env[a[1]] = {156: lambda: math.isnan(left), 157: lambda: math.isinf(left),
                        158: lambda: math.isfinite(left),
                        159: lambda: 0 < (env[a[2]] & 0x7f800000) < 0x7f800000,
                        160: lambda: bool(env[a[2]] & 0x80000000),
                        161: lambda: not (math.isnan(left) or math.isnan(right)) and left != right,
                        162: lambda: not (math.isnan(left) or math.isnan(right)),
                        163: lambda: math.isnan(left) or math.isnan(right)}[op]()
                elif op in {136, 140, 141}:
                    left, right = env[a[2]], env[a[3]]
                    if op == 141:
                        a_value, b_value = finite_value(left), finite_value(right)
                        env[a[1]] = round_bits(a_value - b_value * (a_value // b_value))
                    else:
                        env[a[1]] = fp_operation('/' if op == 136 else '%', left, right)
                elif op in {128, 130, 132}:
                    left, right = env[a[2]], env[a[3]]
                    env[a[1]] = {128: lambda: left + right, 130: lambda: left - right,
                                 132: lambda: left * right}[op]() & 0xffffffff
                elif op in {129, 131, 133}:
                    left, right = env[a[2]], env[a[3]]
                    if (left & 0x7f800000) == 0x7f800000 or (right & 0x7f800000) == 0x7f800000:
                        if op != 129:
                            raise OracleLimit('M4 non-finite arithmetic not qualified')
                        env[a[1]] = f32_bits(bits_f32(left) + bits_f32(right))
                    else:
                        env[a[1]] = operation(op, left, right)
                elif 180 <= op <= 191:
                    left, right = bits_f32(env[a[2]]), bits_f32(env[a[3]])
                    unordered = math.isnan(left) or math.isnan(right)
                    relation = (op - 180) // 2
                    answer = (left == right, left != right, left < right,
                              left > right, left <= right, left >= right)[relation]
                    env[a[1]] = (unordered or answer) if op % 2 else (not unordered and answer)
                elif op == 12:
                    imported = self.defs.get(a[2])
                    if imported is None or imported[0] != 11 or base.literal_string(imported[1][1:])[0] != 'GLSL.std.450':
                        raise OracleLimit('unknown extended instruction import')
                    left, right = env[a[4]], env[a[5]]
                    if a[3] == 40:
                        env[a[1]] = left if exact_value(left) >= exact_value(right) else right
                    elif a[3] == 41:
                        env[a[1]] = max(left, right)
                    elif a[3] == 42:
                        env[a[1]] = left if signed(left) >= signed(right) else right
                    elif a[3] in {37,38,39}:
                        l, r = (bits_f32(left), bits_f32(right)) if a[3] == 37 else (signed(left), signed(right)) if a[3] == 39 else (left,right)
                        env[a[1]] = left if l <= r else right
                    elif a[3] == 50:
                        # Mutation control: one exact product+sum, then a single rounding.
                        env[a[1]] = round_bits(finite_value(left)*finite_value(right)+finite_value(env[a[6]]))
                    elif a[3] in {43,44,45}:
                        upper = env[a[6]]
                        convert = bits_f32 if a[3] == 43 else signed if a[3] == 45 else int
                        env[a[1]] = right if convert(left) < convert(right) else upper if convert(left) > convert(upper) else left
                    else:
                        raise OracleLimit('unsupported extended instruction '+str(a[3]))
                elif op in {164, 165, 166, 167, 170, 171, 172, 174, 176, 178}:
                    left, right = env[a[2]], env[a[3]]
                    env[a[1]] = {164: lambda: left == right, 165: lambda: left != right,
                        166: lambda: bool(left or right), 167: lambda: bool(left and right),
                        170: lambda: left == right, 171: lambda: left != right,
                        172: lambda: left > right, 174: lambda: left >= right,
                        176: lambda: left < right, 178: lambda: left <= right}[op]()
                elif op == 168:
                    env[a[1]] = not env[a[2]]
                elif op == 169:
                    env[a[1]] = env[a[3] if env[a[2]] else a[4]]
                elif op == 245:
                    incoming = [a[i] for i in range(2, len(a), 2) if a[i + 1] == previous]
                    if len(incoming) != 1:
                        raise ExecutionError('Phi predecessor missing')
                    env[a[1]] = env[incoming[0]]
                elif op in {249, 250}:
                    target = a[0] if op == 249 else a[1] if env[a[0]] else a[2]
                    if target not in labels:
                        raise ExecutionError('missing branch target')
                    previous, pc = block, labels[target]
                elif op == 255:
                    raise ExecutionError("executed OpUnreachable")
                elif op == 253:
                    return None
                elif op == 254:
                    return env[a[0]]
                else:
                    raise OracleLimit(f'cannot execute {base.NAMES.get(op, op)}')
            raise ExecutionError('function fell off end')

        run(self.entry_fn)
        return accesses
