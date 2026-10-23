"""Source-independent tests of new interpreter facilities using handwritten SPIR-V."""
from __future__ import annotations

import unittest

from compute_oracle_m7 import Module, StepLimit
from fixtures_m5 import storage
from spirv_oracle_base import ExecutionError


def instruction(op: int,*args: int) -> list[int]:
    return [((len(args)+1)<<16)|op,*args]


def module_words(initialize: bool=True,step: int=12) -> list[int]:
    stream=[0x07230203,0x00010300,0,128,0]
    declarations=[(17,1),(14,0,1),(15,5,20,0x6e69616d,0),(16,20,17,1,1,1),
        (71,8,34,0),(71,8,33,0),(71,6,2),(71,5,6,4),(72,6,0,35,0),
        (19,1),(20,2),(21,3,32,0),(32,4,7,3),(29,5,3),(30,6,5),
        (32,7,12,6),(32,9,12,3),(33,10,1),(43,3,11,0),(43,3,12,1),
        (43,3,13,3),(59,7,8,12),(54,1,20,0,10),(248,21),(59,4,22,7)]
    if initialize:declarations.append((62,22,11))
    body=[(249,23),(248,23),(61,3,24,22),(176,2,25,24,13),
        (246,29,28,0),(250,25,26,29),(248,26),(65,9,27,8,11,24),(62,27,24),(249,28),
        (248,28),(61,3,30,22),(128,3,31,30,step),(62,22,31),(249,23),
        (248,29),(253,),(56,)]
    for entry in declarations+body:stream.extend(instruction(*entry))
    return stream


def prepared(words: list[int]) -> Module:
    module=Module(words)
    module.interface(dict(entry='main',n=[1,1,1],builtins=[],structs=[],bindings=[storage('out',0)]))
    return module


def float_compare_module(opcode: int,left: int,right: int) -> list[int]:
    original=module_words()
    end=5
    while original[end]&65535 != 54:end+=original[end]>>16
    stream=original[:end]
    for entry in [(22,40,32),(43,40,41,left),(43,40,42,right),
                  (54,1,20,0,10),(248,21),(opcode,2,43,41,42),
                  (169,3,44,43,12,11),(65,9,45,8,11,11),
                  (62,45,44),(253,),(56,)]:
        stream.extend(instruction(*entry))
    return stream


class TestM7InstructionOracle(unittest.TestCase):
    def test_actual_function_memory_loop(self) -> None:
        module=prepared(module_words());buffers={(0,0):[99]*4}
        trace=module.execute(buffers,{},set())
        self.assertEqual(buffers,{(0,0):[0,1,2,99]})
        self.assertEqual(trace,[('write',(0,0),0),('write',(0,0),1),('write',(0,0),2)])
        self.assertGreater(module.last_opcodes[61],3)
        self.assertGreater(module.last_opcodes[62],3)

    def test_missing_initialization_is_not_zero(self) -> None:
        with self.assertRaisesRegex(ExecutionError,'uninitialized'):
            prepared(module_words(initialize=False)).execute({(0,0):[99]*4},{},set())

    def test_infinite_execution_is_unfinished(self) -> None:
        with self.assertRaises(StepLimit):
            prepared(module_words(step=11)).execute({(0,0):[99]*4},{},set(),step_limit=500)

    def test_float_unordered_relation_is_not_ordered(self) -> None:
        for opcode,want in [(180,0),(181,1),(182,0),(183,1),
                            (184,0),(185,1),(186,0),(187,1),
                            (188,0),(189,1),(190,0),(191,1)]:
            with self.subTest(opcode=opcode):
                buffers={(0,0):[99]}
                prepared(float_compare_module(opcode,0x7fc00001,0x3f800000)).execute(buffers,{},set())
                self.assertEqual(buffers[(0,0)],[want])

    def test_float_signed_zero_relations(self) -> None:
        for opcode,want in [(180,1),(182,0),(184,0),(188,1),(190,1)]:
            with self.subTest(opcode=opcode):
                buffers={(0,0):[99]}
                prepared(float_compare_module(opcode,0,0x80000000)).execute(buffers,{},set())
                self.assertEqual(buffers[(0,0)],[want])

    def test_instruction_guard_not_host_robustness(self) -> None:
        with self.assertRaisesRegex(ExecutionError,'OOB'):
            prepared(module_words()).execute({(0,0):[]},{},set())

if __name__=='__main__':unittest.main()
