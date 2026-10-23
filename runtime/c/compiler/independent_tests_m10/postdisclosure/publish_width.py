"""Explicit validator resource-limit observation for sealed65533-member boundary."""
from pathlib import Path
import argparse,json,hashlib,struct,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'source'))
from boundaries_m10 import width_case
from abi_m10 import Compiler
from seal_m10 import verify_seal
p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args();verify_seal();assert not a.receipt.exists()
f=width_case(65533);c=Compiler(a.library,f['caps']);status,r,_=c.call(f['source'],7);assert status==0 and r.required_is_exact
status,out,arrays=c.call(f['source'],3,r.required.fields());assert status==0
words=list(arrays['words']);raw=struct.pack('<'+'I'*len(words),*words);widths=[];i=5
while i<len(words):
 n=words[i]>>16;op=words[i]&65535
 if op==30:widths.append(n)
 assert 0<n<=65535;i+=n
assert 65535 in widths and i==len(words)
with tempfile.TemporaryDirectory(prefix='doe-m10-wide-val-',dir='/tmp') as d:
 module=Path(d)/'wide.spv';module.write_bytes(raw);checks=[]
 for flags in [[],['--max-struct-members','65533']]:
  command=[a.validator,'--target-env','vulkan1.1',*flags,str(module)];v=subprocess.run(command,capture_output=True,text=True);checks.append(dict(command=command,returncode=v.returncode,stdout=v.stdout,stderr=v.stderr))
assert checks[0]['returncode']!=0 and 'members' in checks[0]['stderr']
assert checks[1]['returncode']==0
report=dict(source_sha256=hashlib.sha256(f['source'].encode()).hexdigest(),library_sha256=hashlib.sha256(a.library.read_bytes()).hexdigest(),module_sha256=hashlib.sha256(raw).hexdigest(),word_count=len(words),OpTypeStruct_word_counts=widths,checks=checks,default_target_validation=False,raised_resource_target_validation=True,physical_device=False)
a.receipt.write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
