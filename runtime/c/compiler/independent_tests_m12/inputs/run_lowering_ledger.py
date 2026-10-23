from pathlib import Path
from collections import Counter
import argparse,json,hashlib,struct,subprocess
from abi_m10 import Compiler
ROOT=Path(__file__).resolve().parent

def main():
 p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(exist_ok=False)
 for line in (ROOT/'LOWERING_LEDGER.sha256').read_text().splitlines():
  expected,name=line.split(None,1);assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==expected,name
 c=Compiler(a.library);rows=[]
 for case in json.loads((ROOT/'LOWERING_LEDGER.json').read_text())['cases']:
  try:
   source=(ROOT/case['source']).read_text();s,r,_=c.call(source,7);assert s==0,(s,bytes(r.diagnostic.message));s,r,arrays=c.call(source,3,r.required.fields());assert s==0
   words=list(arrays['words']);raw=struct.pack('<'+'I'*len(words),*words);target=a.out/(case['id']+'.spv');target.write_bytes(raw);v=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True);assert v.returncode==0,v.stderr
   instructions=[];at=5
   while at<len(words):
    wc=words[at]>>16;assert wc>0;instructions.append((words[at]&65535,words[at+1:at+wc],wc));at+=wc
   functions=[];current=None
   for op,args,wc in instructions:
    if op==54:current=[]
    elif op==56:functions.append(current);current=None
    elif current is not None:current.append((op,args,wc))
   bodies=[[i for i in f if i[0] not in [55,248,253,254]] for f in functions]
   body=[b for b in bodies if b];assert len(body)==1,('nonempty bodies',len(body));body=body[0]
   ops={str(k):v for k,v in Counter(i[0] for i in body).items()};assert ops==case['body_opcodes'],('opcode ledger',ops,case['body_opcodes'])
   assert sum(i[2] for i in body)==case['body_words'];assert len({i[1][1] for i in body})==case['body_result_ids']
   imports=[i for i in instructions if i[0]==11];assert len(imports)==int(case['uses_extended_import']);assert all(i[2]==6 for i in imports)
   row={'id':case['id'],'passed':True,'ids':len(body),'words':sum(i[2] for i in body),'opcodes':ops,'target_sha256':hashlib.sha256(raw).hexdigest()}
  except Exception as e:row={'id':case['id'],'passed':False,'error':repr(e)}
  rows.append(row);print(row,flush=True)
 (a.out/'results.json').write_text(json.dumps({'cases':rows,'failed':[r for r in rows if not r['passed']]},indent=2,sort_keys=True)+'\n');return int(any(not r['passed'] for r in rows))
if __name__=='__main__':raise SystemExit(main())
