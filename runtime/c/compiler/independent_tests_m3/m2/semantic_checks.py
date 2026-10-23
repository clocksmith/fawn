"""Exact-vector M2 execution checks layered over unchanged M1 float reference."""
from semantic_checks_m1 import *
import semantic_checks_m1 as old

def run_vectors(module, expected):
    if 'vectors' not in expected:
        return old.run_vectors(module, expected)
    readonly={(b['group'],b['binding']) for b in expected['bindings'] if b['access']==1}
    trials=0
    for number,vec in enumerate(expected['vectors']):
        for y,z in [(0,0),(37,91),(0xffffffff,0x80000000)]:
            actual={key:list(data) for key,data in vec['initial'].items()}
            accesses=module.execute(actual,vec['x'],readonly,y,z)
            if actual!=vec['want']:
                raise ExecutionError(f"M2 vector {number} x={vec['x']} y={y} z={z}: {actual} != {vec['want']}")
            if 'accesses' in vec and accesses!=vec['accesses']:
                raise ExecutionError(f'M2 vector {number}: effects {accesses} != {vec["accesses"]}')
            trials+=1
    return trials
