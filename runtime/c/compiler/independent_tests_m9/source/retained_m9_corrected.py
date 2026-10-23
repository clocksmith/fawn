"""Postinspection adapter repair, not a fixture or candidate change.

The presealed semantic_m9.execute_vectors omitted historical unfinished handling.
Only fixtures already carrying unfinished=True use the unchanged M8 vector
routine, with its exception identity rebound to M9. Finite vectors keep the
presealed M9 routine unchanged. Original sources/first receipts remain intact.
"""
import sys
import retained_m9 as original
import semantic_m8 as historical
from compute_oracle_m9 import StepLimit
from semantic_m9 import execute_vectors as finite_vectors
from spirv_oracle_base import ExecutionError

historical.StepLimit=StepLimit

def execute_vectors(module, expected):
    if expected.get('unfinished'):
        return historical.execute_vectors(module,expected)
    return finite_vectors(module,expected)

original.execute_vectors=execute_vectors


def controls():
    class Fake:
        last_steps=1;last_opcodes={};last_calls=[]
        def __init__(self,unfinished):self.unfinished=unfinished
        def execute(self,*args,**kwargs):
            if self.unfinished:raise StepLimit('independent control bound')
            return []
    expected=dict(bindings=[],vectors=[dict(initial={},want={},inputs={})])
    assert execute_vectors(Fake(False),expected)['cpu_invocations']==1
    try:execute_vectors(Fake(True),expected)
    except StepLimit:pass
    else:raise AssertionError('finite StepLimit was masked')
    expected['unfinished']=True
    assert execute_vectors(Fake(True),expected)['unfinished']==1
    try:execute_vectors(Fake(False),expected)
    except ExecutionError:pass
    else:raise AssertionError('unexpected termination was masked')
    print('PASS four independent unfinished adapter controls')


if __name__=='__main__':
    if '--controls' in sys.argv:controls()
    else:
        if '--only-unfinished' in sys.argv:
            sys.argv.remove('--only-unfinished')
            families=original.old.families
            original.old.families=lambda repo:[(f,[(n,s,e) for n,s,e in good if e.get('unfinished')],[]) for f,good,bad in families(repo)]
        raise SystemExit(original.main())
