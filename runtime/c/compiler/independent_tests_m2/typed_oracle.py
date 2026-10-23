"""Independent typed SPIR-V instruction oracle. No WGSL is interpreted here.

The prior u32 oracle parser is reused with attribution in the acceptance receipt.
Execution and multi-resource interface checking are independently extended.
"""
from __future__ import annotations

import math
import struct

import spirv_oracle_base as base
from spirv_oracle_base import ExecutionError, OracleLimit, Pointer, StructuralError

base.NAMES.update({22: 'TypeFloat', 46: 'ConstantNull', 129: 'FAdd', 255: 'Unreachable'})
base.RESULT_FIRST.add(22)
base.RESULT_SECOND.update({46, 129})
base.MIN_ARGS.update({22: 2, 46: 2, 129: 4, 130: 4, 170: 4, 171: 4, 172: 4, 174: 4, 176: 4, 178: 4, 245: 4, 255: 0})


def f32_bits(value: float) -> int:
    try:
        return struct.unpack('<I', struct.pack('<f', value))[0]
    except OverflowError:
        return 0xff800000 if value < 0 else 0x7f800000


def bits_f32(value: int) -> float:
    return struct.unpack('<f', struct.pack('<I', value))[0]


def float_equal(actual: int, expected: int) -> bool:
    if math.isnan(bits_f32(expected)):
        return math.isnan(bits_f32(actual))
    return actual == expected


class Module(base.Module):
    def __init__(self, words: list[int]) -> None:
        super().__init__(words)
        for op, args, _ in self.instructions:
            if op == 22:
                self.types[args[0]] = (op, args[1:])
            elif op == 46:
                if self.types[args[0]] not in {(21, (32, 0)), (22, (32,))}:
                    raise OracleLimit('non-scalar OpConstantNull')
                self.constants[args[1]] = 0

    def interface(self, expected: dict) -> dict:
        if self.version != 0x00010300:
            raise StructuralError('expected SPIR-V 1.3')
        if 1 not in self.capabilities or self.memory_models != [(0, 1)]:
            raise StructuralError('wrong shader memory model/capability')
        if len(self.entries) != 1:
            raise StructuralError('expected one entry point')
        model, fn, name, interfaces = self.entries[0]
        if model != 5 or name != expected['entry'] or fn not in self.functions:
            raise StructuralError('wrong compute entry point')
        if (fn, 17, expected['n'], 1, 1) not in self.modes:
            raise StructuralError('wrong local size')
        self.entry_fn = fn
        self.resources = {}
        for rid, (ptr_id, storage, _) in self.variables.items():
            if storage != 12:
                continue
            decorations = self.decorations.get(rid, {})
            try:
                key = (decorations[34][0], decorations[33][0])
            except (KeyError, IndexError) as exc:
                raise StructuralError('missing descriptor identity') from exc
            if key in self.resources.values():
                raise StructuralError('duplicate descriptor identity')
            self.resources[rid] = key
            op, ptr = self.types[ptr_id]
            if op != 32 or ptr[0] != 12:
                raise StructuralError('wrong storage pointer type')
            struct_id = ptr[1]
            op, members = self.types[struct_id]
            if op != 30 or len(members) != 1:
                raise StructuralError('expected one-member resource struct')
            if 2 not in self.decorations.get(struct_id, {}):
                raise StructuralError('Block decoration missing')
            member_dec = self.member_decorations.get((struct_id, 0), {})
            if member_dec.get(35) != (0,):
                raise StructuralError('resource member offset must be zero')
            array_id = members[0]
            op, args = self.types[array_id]
            if op != 29 or self.decorations.get(array_id, {}).get(6) != (4,):
                raise StructuralError('wrong runtime array or stride')
            scalar = self.types[args[0]]
            candidates = [b for b in expected['bindings'] if (b['group'], b['binding']) == key]
            if len(candidates) != 1:
                raise StructuralError(f'unexpected descriptor {key}')
            exp = candidates[0]
            want = (21, (32, 0)) if exp['scalar'] == 1 else (22, (32,))
            if scalar != want:
                raise StructuralError(f'wrong scalar for {key}: {scalar} != {want}')
            if exp['access'] == 1 and 24 not in decorations and 24 not in member_dec:
                raise StructuralError(f'read-only descriptor {key} lacks NonWritable')
        if len(self.resources) != len(expected['bindings']):
            raise StructuralError('missing declared resource')
        inputs = [rid for rid, (_, sc, _) in self.variables.items()
                  if sc == 1 and self.decorations.get(rid, {}).get(11) == (28,)]
        if len(inputs) != 1:
            raise StructuralError('expected one GlobalInvocationId')
        self.input = inputs[0]
        op, ptr = self.types[self.variables[self.input][0]]
        if op != 32 or ptr[0] != 1:
            raise StructuralError('wrong input pointer')
        op, vector = self.types[ptr[1]]
        if op != 23 or vector[1] != 3 or self.types[vector[0]] != (21, (32, 0)):
            raise StructuralError('wrong global invocation type')
        if self.input not in interfaces:
            raise StructuralError('GlobalInvocationId omitted from interface')
        if any(self.variables.get(i, (None, None, None))[1] not in {1, 3}
               for i in interfaces):
            raise StructuralError('SPIR-V 1.3 entry interface includes storage')
        return {'word_count': len(self.words), 'instruction_count': len(self.instructions),
                'opcodes': sorted({op for op, _, _ in self.instructions})}

    def execute(self, buffers: dict[tuple[int, int], list[int]], x: int,
                read_only: set[tuple[int, int]], y: int = 37, z: int = 91) -> list:
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
            if ptr.root == self.input:
                obj = (x, y, z)
                for index in ptr.indices:
                    obj = obj[index]
                return obj
            if ptr.root in self.resources:
                key, index = storage_target(ptr)
                accesses.append(('read', key, index))
                return buffers[key][index]
            obj = memory[ptr.root]
            for index in ptr.indices:
                obj = obj[index]
            return obj

        def store(ptr: Pointer, value: int) -> None:
            if not isinstance(ptr, Pointer):
                raise ExecutionError('store to non-pointer')
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

        def run(fn: int, arguments: tuple = (), depth: int = 0):
            if depth > 10:
                raise ExecutionError('function recursion budget exceeded')
            body = self.functions[fn]
            labels = {a[0]: i for i, (op, a, _) in enumerate(body) if op == 248}
            pc, steps, parameter_index = 0, 0, 0
            block = previous = None
            while pc < len(body):
                steps += 1
                if steps > 2000:
                    raise ExecutionError('execution instruction budget exceeded')
                op, a, _ = body[pc]
                pc += 1
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
                elif op in {0, 8, 247}:
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
                elif op in {83, 113, 124}:
                    env[a[1]] = env[a[2]]
                elif op in {128, 130, 132}:
                    left, right = env[a[2]], env[a[3]]
                    env[a[1]] = {128: lambda: left + right, 130: lambda: left - right,
                                 132: lambda: left * right}[op]() & 0xffffffff
                elif op == 129:
                    left, right = bits_f32(env[a[2]]), bits_f32(env[a[3]])
                    env[a[1]] = f32_bits(left + right)
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
