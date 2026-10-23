"""Independent M5 raw-byte uniform oracle. Execution loop adapted from M4 typed_oracle.
No compiler implementation imports and no reflection-driven host execution.
SPIR-V decorations/type IDs and access-chain member indices address host bytes.
"""
from __future__ import annotations
import struct
from rational_oracle import operation, exact_value
import spirv_oracle_base as base
from typed_oracle import Module as StorageModule, f32_bits, bits_f32
from spirv_oracle_base import ExecutionError, OracleLimit, Pointer, StructuralError

class BindingAdmissionError(ExecutionError):
    pass

class Module(StorageModule):
    def interface(self, expected: dict) -> dict:
        storage = dict(expected, n=expected['n'][0], bindings=[b for b in expected['bindings'] if b['kind'] == 1])
        receipt = super().interface(storage)
        self.uniforms = {}
        self.uniform_minimums = {}
        expected_uniforms = {(b['group'], b['binding']): b for b in expected['bindings'] if b['kind'] == 2}
        for rid, (ptr_id, storage_class, _) in self.variables.items():
            if storage_class != 2:
                continue
            dec = self.decorations.get(rid, {})
            if 34 not in dec or 33 not in dec:
                raise StructuralError('uniform descriptor identity absent')
            key = (dec[34][0], dec[33][0])
            if key not in expected_uniforms or key in self.resources.values() or key in self.uniform_minimums:
                raise StructuralError('unexpected/duplicate uniform descriptor')
            op, pointer = self.types[ptr_id]
            if op != 32 or pointer[0] != 2:
                raise StructuralError('uniform pointer has incorrect storage class')
            struct_id = pointer[1]
            op, members = self.types[struct_id]
            if op != 30 or 2 not in self.decorations.get(struct_id, {}):
                raise StructuralError('uniform lacks struct Block type')
            spec = expected['structs'][expected_uniforms[key]['struct_index']]
            if len(members) != len(spec['members']):
                raise StructuralError('uniform emitted member count differs')
            for index, (type_id, member) in enumerate(zip(members, spec['members'])):
                scalar = (21, (32, 0)) if member['scalar'] == 1 else (22, (32,))
                if self.types[type_id] != scalar:
                    raise StructuralError('uniform member type differs')
                if self.member_decorations.get((struct_id, index), {}).get(35) != (index * 4,):
                    raise StructuralError('uniform member Offset differs')
            self.uniforms[rid] = (key, struct_id)
            self.uniform_minimums[key] = len(members) * 4
        if len(self.uniforms) != len(expected_uniforms):
            raise StructuralError('missing uniform resource')
        return receipt

    def execute(self, buffers: dict[tuple[int, int], list[int]], x: int,
                read_only: set[tuple[int, int]], y: int = 37, z: int = 91,
                uniforms: dict[tuple[int, int], bytes] | None = None) -> list:
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
            if ptr.root == self.input:
                obj = (x, y, z)
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
                elif op in {129, 131, 133}:
                    left, right = env[a[2]], env[a[3]]
                    if (left & 0x7f800000) == 0x7f800000 or (right & 0x7f800000) == 0x7f800000:
                        if op != 129:
                            raise OracleLimit('M4 non-finite arithmetic not qualified')
                        env[a[1]] = f32_bits(bits_f32(left) + bits_f32(right))
                    else:
                        env[a[1]] = operation(op, left, right)
                elif op in {184, 186, 188, 190}:
                    left, right = exact_value(env[a[2]]), exact_value(env[a[3]])
                    env[a[1]] = {184:lambda:left<right,186:lambda:left>right,
                                  188:lambda:left<=right,190:lambda:left>=right}[op]()
                elif op == 12:
                    imported = self.defs.get(a[2])
                    if imported is None or imported[0] != 11 or base.literal_string(imported[1][1:])[0] != 'GLSL.std.450':
                        raise OracleLimit('unknown extended instruction import')
                    left, right = env[a[4]], env[a[5]]
                    if a[3] == 40:
                        env[a[1]] = left if exact_value(left) >= exact_value(right) else right
                    elif a[3] == 41:
                        env[a[1]] = max(left, right)
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
