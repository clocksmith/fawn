"""M2 adapter retaining every M1 positive and explicit still-invalid cases."""
from fixtures_m1 import *
import fixtures_m1 as m1
import m2_fixtures as m2

def admitted(root):
    return m1.admitted(root) + m2.admitted(root)

def rejected():
    return [case for case in m1.rejected() if case[0] not in m2.LIFTS] + m2.rejected()
