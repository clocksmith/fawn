# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""M9 developer instruction model, derived from the retained M8 scalar model.

Explicit function frames execute emitted calls/parameters/returns and snapshot
arguments. Python math here is only a deterministic execution choice, never an
independent transcendental accuracy oracle or physical-device qualification.
"""
from __future__ import annotations
import struct
import math
if __package__:
    from .test_cli import Instruction, Descriptor, f32, float_bits
else:
    from test_cli import Instruction, Descriptor, f32, float_bits

def execute_functions(instructions: list[Instruction], memory: dict[Descriptor, list[int]],
            invocation: int | tuple[int, int, int],
            uniforms: dict[Descriptor, bytes] | None = None,
            inputs: dict[int, int | tuple[int, int, int]] | None = None,
            step_limit: int = 100000) -> None:
    """Execute actual instructions, asserting bounds at pointer and load/store use."""
    values: dict[int, int | tuple] = {}
    local_memory: dict[int, int | None] = {}
    labels: dict[int, int] = {}
    decorations: dict[int, dict[int, int]] = {}
    readonly: set[int] = set()
    variables = []
    pointers: dict[int, int] = {}
    members: dict[int, tuple[int, ...]] = {}
    offsets: dict[tuple[int, int], int] = {}
    uniforms = {} if uniforms is None else uniforms
    inputs = ({28: (invocation, 0, 0) if isinstance(invocation, int) else invocation}
              if inputs is None else inputs)
    entry = None
    body_start = None
    functions = {}
    current_function = None
    for pc, (op, args) in enumerate(instructions):
        if op == 43:  # Constant scalar bit pattern.
            values[args[1]] = args[2]
        elif op in (41, 42):
            values[args[1]] = int(op == 41)
        elif op == 71:
            if args[1] == 24:
                readonly.add(args[0])
            elif len(args) == 3:
                decorations.setdefault(args[0], {})[args[1]] = args[2]
        elif op == 30:
            members[args[0]] = args[1:]
        elif op == 32:
            pointers[args[0]] = args[2]
        elif op == 72 and args[2] == 35:
            offsets[(args[0], args[1])] = args[3]
        elif op == 59 and args[2] != 7:
            variables.append((args[1], args[2], args[0]))
        elif op == 15:
            entry = args[1]
        elif op == 54:
            current_function = args[1]
            functions[current_function] = [pc + 1, []]
            if current_function == entry:
                body_start = pc + 1
        elif op == 55:
            functions[current_function][1].append(args[1])
            functions[current_function][0] = pc + 1
        elif op == 248:
            labels[args[0]] = pc
    assert entry is not None and body_start is not None
    for ident, storage, pointer_type in variables:
        if storage == 1:
            values[ident] = ("input", decorations[ident][11])
        elif storage == 12:
            descriptor = (decorations[ident][34], decorations[ident][33])
            assert descriptor in memory
            values[ident] = ("buffer", descriptor, ident)
        elif storage == 2:
            descriptor = (decorations[ident][34], decorations[ident][33])
            type_id = pointers[pointer_type]
            minimum = max(offsets[(type_id, i)] + 4 for i in range(len(members[type_id])))
            assert len(uniforms[descriptor]) >= minimum, "short uniform binding rejected before invocation"
            values[ident] = ("uniform", descriptor, type_id)
        elif storage == 7:
            values[ident] = ("local", ident, local_memory)
            local_memory[ident] = None
        else:
            raise AssertionError(("unexpected storage class", storage))
    global_values = values.copy()
    frames = []
    pc, steps = body_start, 0
    label = previous = None
    while True:
        steps += 1
        assert steps < step_limit, "execution unfinished at harness step bound"
        op, args = instructions[pc]
        pc += 1
        if op == 248:
            label = args[0]
        elif op in (247, 246):
            continue
        elif op == 59:
            assert args[2] == 7
            values[args[1]] = ("local", args[1], local_memory)
            local_memory[args[1]] = None
        elif op == 57:
            start, parameters = functions[args[2]]
            arguments = [values[argument] for argument in args[3:]]
            assert len(parameters) == len(arguments)
            frames.append((pc, label, previous, values, local_memory, args[1]))
            values = global_values.copy()
            values.update(zip(parameters, arguments))
            local_memory = {}
            pc, label, previous = start, None, None
        elif op == 12:
            assert len(args) == 5 and args[3] in (21, 27, 31)
            value = f32(values[args[4]])
            if args[3] == 21:
                answer = math.tanh(value)
            elif args[3] == 27:
                try:
                    answer = math.exp(value)
                except OverflowError:
                    answer = math.inf
            else:
                answer = math.sqrt(value) if value >= 0 else math.nan
            values[args[1]] = float_bits(answer)
        elif op == 61:
            pointer = values[args[2]]
            if pointer[0] == "input":
                values[args[1]] = inputs[pointer[1]]
            elif pointer[0] == "local":
                assert pointer[2][pointer[1]] is not None, "uninitialized Function local read"
                values[args[1]] = pointer[2][pointer[1]]
            elif pointer[0] == "member":
                values[args[1]] = struct.unpack_from("<I", uniforms[pointer[1]], pointer[2])[0]
            else:
                assert pointer[0] == "element"
                buffer, index = memory[pointer[1]], pointer[2]
                assert 0 <= index < len(buffer), "out of bounds read"
                values[args[1]] = buffer[index]
        elif op == 81:
            values[args[1]] = values[args[2]][args[3]]
        elif op == 68:
            pointer = values[args[2]]
            assert pointer[0] == "buffer" and args[3] == 0
            values[args[1]] = len(memory[pointer[1]])
        elif op == 168:
            values[args[1]] = not values[args[2]]
        elif op in (164, 165):
            values[args[1]] = (values[args[2]] == values[args[3]]) == (op == 164)
        elif op in (170, 171, 172, 174, 176, 178):
            left, right = values[args[2]], values[args[3]]
            comparisons = {170: left == right, 171: left != right,
                           172: left > right, 174: left >= right,
                           176: left < right, 178: left <= right}
            values[args[1]] = comparisons[op]
        elif op == 250:
            previous = label
            pc = labels[args[1] if values[args[0]] else args[2]]
        elif op == 249:
            previous = label
            pc = labels[args[0]]
        elif op == 245:
            matches = [args[i] for i in range(2, len(args), 2)
                       if args[i + 1] == previous]
            assert len(matches) == 1
            values[args[1]] = values[matches[0]]
        elif op == 65:
            pointer = values[args[2]]
            if pointer[0] == "uniform":
                assert len(args) == 4
                member_index = values[args[3]]
                assert 0 <= member_index < len(members[pointer[2]])
                offset = offsets[(pointer[2], member_index)]
                assert offset + 4 <= len(uniforms[pointer[1]])
                values[args[1]] = ("member", pointer[1], offset)
            else:
                assert pointer[0] == "buffer" and values[args[3]] == 0
                index = values[args[4]]
                assert 0 <= index < len(memory[pointer[1]]), "unguarded element pointer"
                values[args[1]] = ("element", pointer[1], index, pointer[2])
        elif op == 124:
            values[args[1]] = values[args[2]]
        elif op in (126, 127, 200):
            x = values[args[2]]
            values[args[1]] = (-x if op == 126 else ~x) & 0xffffffff if op != 127 else float_bits(-f32(x))
        elif op in (166, 167):
            x, y = bool(values[args[2]]), bool(values[args[3]])
            values[args[1]] = int((x or y) if op == 166 else (x and y))
        elif op in (173, 175, 177, 179):
            x, y = signed(values[args[2]]), signed(values[args[3]])
            values[args[1]] = {173:x>y,175:x>=y,177:x<y,179:x<=y}[op]
        elif op in (194, 195, 196, 197, 198, 199):
            x, y = values[args[2]], values[args[3]]
            if op in (194,195,196):
                assert y < 32, "undefined target shift count"
                z = x >> y if op == 194 else signed(x) >> y if op == 195 else x << y
            else:
                z = x | y if op == 197 else x ^ y if op == 198 else x & y
            values[args[1]] = z & 0xffffffff
        elif op in (134,135,137,138):
            x, y = values[args[2]], values[args[3]]
            if op in (135,138): x, y = signed(x), signed(y)
            assert y != 0 and not (x == -2147483648 and y == -1), "undefined target integer division"
            q = abs(x)//abs(y) * (-1 if (x<0)!=(y<0) else 1)
            values[args[1]] = (q if op in (134,135) else x-q*y) & 0xffffffff
        elif op in (136,140):
            x, y = f32(values[args[2]]), f32(values[args[3]])
            values[args[1]] = float_bits(x/y if op == 136 else math.fmod(x,y))
        elif op == 156:
            values[args[1]] = int(math.isnan(f32(values[args[2]])))
        elif op in (109,110):
            x = f32(values[args[2]])
            assert math.isfinite(x) and (0 <= x <= 4294967295 if op == 109 else -2147483648 <= x <= 2147483647), "undefined target float conversion"
            values[args[1]] = math.trunc(x) & 0xffffffff
        elif op in (111,112):
            x = values[args[2]]
            values[args[1]] = float_bits(signed(x) if op == 111 else x)
        elif op == 128:
            values[args[1]] = (values[args[2]] + values[args[3]]) & 0xFFFFFFFF
        elif op == 130:
            values[args[1]] = (values[args[2]] - values[args[3]]) & 0xFFFFFFFF
        elif op == 132:
            values[args[1]] = (values[args[2]] * values[args[3]]) & 0xFFFFFFFF
        elif op in (129, 131, 133):
            left, right = f32(values[args[2]]), f32(values[args[3]])
            values[args[1]] = float_bits(left + right if op == 129 else
                                       left - right if op == 131 else left * right)
        elif op in (180, 183, 184, 186, 188, 190):
            left, right = f32(values[args[2]]), f32(values[args[3]])
            values[args[1]] = {180: left == right, 183: left != right, 184: left < right,
                              186: left > right, 188: left <= right, 190: left >= right}[op]
        elif op == 169:
            values[args[1]] = values[args[3] if values[args[2]] else args[4]]
        elif op == 62:
            pointer = values[args[0]]
            if pointer[0] == "local":
                pointer[2][pointer[1]] = values[args[1]]
                continue
            assert pointer[0] == "element" and pointer[3] not in readonly
            buffer, index = memory[pointer[1]], pointer[2]
            assert 0 <= index < len(buffer), "out of bounds write"
            buffer[index] = values[args[1]]
        elif op in (253, 254):
            returned = values[args[0]] if op == 254 else None
            if not frames:
                assert op == 253
                return
            pc, label, previous, values, local_memory, result = frames.pop()
            if returned is not None:
                values[result] = returned
        elif op == 255:
            raise AssertionError("executed unreachable merge")
        else:
            raise AssertionError(("unexpected instruction", op, args))

def signed(bits):
    return bits - 0x100000000 if bits & 0x80000000 else bits
