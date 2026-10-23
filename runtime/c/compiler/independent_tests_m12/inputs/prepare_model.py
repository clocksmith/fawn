"""Source-bound materialization of accepted M11 model plus explicit M12 dispatch."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parent
BASE=Path('/workspace/shared/doe-c-migration-20261023/runtime/c/compiler/independent_tests_m11/oracle')
s=(BASE/'target_m11.py').read_text()
s=s.replace('RESULT2={12,41,', 'RESULT2={148,12,41,')
s=s.replace('from interval_math_m9 import reference', 'from interval_math_m9 import reference\nfrom target_numeric import numeric_instruction')
s=s.replace('            elif op in UNARY|BINARY:env[a[1]]=self.componentwise', "            elif op==148:env[a[1]]=numeric_instruction(self,a[0],148,[env[x] for x in a[2:]])\n            elif op in UNARY|BINARY:env[a[1]]=self.componentwise")
a=s.index('                if self.types[a[0]]!=(22,(32,)) and k in {21,27,31}:')
b=s.index('                elif k in {37,38,39,40,41,42,43,44,45}:',a)
s=s[:a]+"                if k in {21,27,31,66,67,68,69}:env[a[1]]=numeric_instruction(self,a[0],k,vals)\n"+s[b:]
(ROOT/'target_m12.py').write_text(s)
(ROOT/'workgroup_m12.py').write_text((BASE/'workgroup_vm.py').read_text().replace('from target_m11 import ', 'from target_m12 import '))
imports={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [BASE/name for name in ['target_m11.py','workgroup_vm.py','abi_m10.py','exact_numeric_m8.py','interval_math_m9.py']]}
(ROOT/'MODEL_IMPORTS.json').write_text(json.dumps(imports,indent=2,sort_keys=True)+'\n')
