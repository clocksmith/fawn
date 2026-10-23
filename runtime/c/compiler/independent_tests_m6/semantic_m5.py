"""Independent M5 admission, layout, raw-byte opcode execution and controls."""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from api_m5 import Api, Reflection, reflection_dict
import fixtures_m5
from uniform_oracle_m5 import Module, BindingAdmissionError
from typed_oracle import ExecutionError, StructuralError, OracleLimit


def reflection_bytes(expected: dict) -> bytes:
    result=Reflection();result.entry_point=expected['entry'].encode()
    result.workgroup_size[:]=expected['n'];result.binding_count=len(expected['bindings']);result.struct_count=len(expected['structs'])
    for dest,spec in zip(result.bindings,expected['bindings']):
        for k,v in spec.items():setattr(dest,k,v.encode() if isinstance(v,str) else v)
    for dest,spec in zip(result.structs,expected['structs']):
        for k,v in spec.items():
            if k=='members':continue
            setattr(dest,k,v.encode() if isinstance(v,str) else v)
        for target,member in zip(dest.members,spec['members']):
            for k,v in member.items():setattr(target,k,v.encode() if isinstance(v,str) else v)
    return bytes(result)


def execute_vectors(module: Module, expected: dict) -> int:
    readonly={(b['group'],b['binding']) for b in expected['bindings'] if b['kind']==1 and b['access']==1}
    count=0
    for v in expected['vectors']:
        for y,z in [(0,0),(37,91),(0xffffffff,0x80000000)]:
            actual={key:list(data) for key,data in v['initial'].items()}
            uniforms={key:bytes(data) for key,data in v['uniforms'].items()}
            module.execute(actual,v['x'],readonly,y,z,uniforms)
            if actual!=v['want']:
                raise ExecutionError(f"wrong result x={v['x']} y={y} z={z}: {actual} != {v['want']}")
            if uniforms!=v['uniforms']:raise ExecutionError('uniform bytes mutated')
            count+=1
    return count


def controls(words: list[int], expected: dict) -> list[dict]:
    base=Module(words);base.interface(expected)
    first_uniform=next(iter(base.uniforms));_,struct_id=base.uniforms[first_uniform]
    changes=[]
    for op,args,offset in base.instructions:
        if op==72 and args[0]==struct_id and args[1]==0 and args[2]==35:
            changes.append(('wrong_member_offset',offset+4,4))
        if op==71 and args[0]==first_uniform and args[1]==33:
            changes.append(('wrong_binding',offset+3,99))
        if op==30 and args[0]==struct_id:
            other=next((tid for tid,t in base.types.items() if t==(22,(32,))),None)
            if other is not None:changes.append(('wrong_member_type',offset+2,other))
        if op==65 and args[2]==first_uniform:
            selected=base.constants.get(args[3]);replacement=next((rid for rid,value in base.constants.items() if value==2 and rid!=args[3] and base.types.get(base.defs[rid][1][0])==(21,(32,0))),None)
            if selected==0 and replacement is not None:changes.append(('wrong_selected_member',offset+4,replacement));break
    # Separate same-type variables must not become aliases despite valid types.
    uniform_roots=list(base.uniforms)
    if len(uniform_roots)>1:
        for op,args,offset in base.instructions:
            if op==65 and args[2]==uniform_roots[0]:
                changes.append(('wrong_uniform_resource',offset+3,uniform_roots[1]));break
    records=[]
    for name,index,value in changes:
        changed=words.copy();changed[index]=value
        assert changed!=words
        try:
            candidate=Module(changed);candidate.interface(expected);execute_vectors(candidate,expected)
        except (ExecutionError,StructuralError,KeyError,IndexError,struct.error) as exc:
            records.append(dict(case=name,passed=True,classification='DEFECT_DETECTED',reason=str(exc)))
        except OracleLimit as exc:
            records.append(dict(case=name,passed=False,classification='UNSUPPORTED_NOT_PASS',reason=str(exc)))
        else:records.append(dict(case=name,passed=False,classification='DEFECT_ESCAPED'))
    return records


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['repo','library','validator','evidence']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();args.evidence.mkdir(parents=True,exist_ok=False)
    modules=args.evidence/'modules';modules.mkdir();sources=args.evidence/'sources';sources.mkdir()
    api=Api(args.library);records=[];positive=fixtures_m5.admitted(args.repo);negative=fixtures_m5.rejected();compiled={}
    for name,source,expected in positive:
        (sources/(name+'.wgsl')).write_bytes(source)
        status,result,words,errors=api.call(source)
        detail=dict(case=name,source_sha256=hashlib.sha256(source).hexdigest(),status=status,diagnostic=result.diagnostic.message.decode(errors='replace'))
        if status:errors.append('admitted source rejected')
        else:
            if bytes(result.reflection)!=reflection_bytes(expected):errors.append('reflection differs including unused bytes: '+repr(reflection_dict(result.reflection)))
            raw=struct.pack('<'+'I'*len(words),*words);path=modules/(name+'.spv');path.write_bytes(raw)
            detail['output_sha256']=hashlib.sha256(raw).hexdigest()
            val=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,timeout=15)
            if val.returncode:errors.append('spirv-val: '+val.stderr)
            try:
                module=Module(words);detail['structure']=module.interface(expected);detail['cpu_trials']=execute_vectors(module,expected)
                first=expected['vectors'][0]
                for key,minimum in module.uniform_minimums.items():
                    for length in [0,minimum-1]:
                        short=dict(first['uniforms']);short[key]=short[key][:length]
                        initial={k:list(v) for k,v in first['initial'].items()}
                        try:module.execute(initial,first['x'],set(),uniforms=short)
                        except BindingAdmissionError:pass
                        else:raise ExecutionError('short uniform admitted')
                        assert initial==first['initial'],'short uniform touched storage before rejection'
                detail['short_uniform_checks']=2*len(module.uniform_minimums)
                compiled[name]=(words,expected)
            except (ExecutionError,StructuralError,OracleLimit,AssertionError,KeyError,IndexError,ValueError) as exc:errors.append(type(exc).__name__+': '+str(exc))
        records.append(dict(**detail,passed=not errors,errors=errors));print(json.dumps(records[-1]) if errors else 'PASS '+name,flush=True)
    for name,source in negative:
        (sources/(name+'.wgsl')).write_bytes(source);status,result,_,errors=api.call(source)
        if not status:errors.append('invalid/excluded source accepted')
        records.append(dict(case=name,passed=not errors,errors=errors,status=status,offset=result.diagnostic.offset,diagnostic=result.diagnostic.message.decode(errors='replace'),source_sha256=hashlib.sha256(source).hexdigest()))
        if errors:print(json.dumps(records[-1]),flush=True)
        # Every failure must preserve the workspace's ability to compile again.
        st,_,_,problems=api.call(positive[0][1])
        if st or problems:records.append(dict(case=name+'_reuse',passed=False,errors=problems+[str(st)]))
    mutation_records=[]
    for name in ['all_members_3','shared_type_resources']:
        if name in compiled:
            for row in controls(*compiled[name]):mutation_records.append(dict(source=name,**row))
    required={'wrong_member_offset','wrong_binding','wrong_member_type','wrong_selected_member','wrong_uniform_resource'}
    observed={r['case'] for r in mutation_records}
    if not required<=observed:mutation_records.append(dict(case='mutation_coverage',passed=False,missing=sorted(required-observed)))
    report=dict(positive_count=len(positive),negative_count=len(negative),checks=len(records),passed=sum(r['passed'] for r in records),cpu_trials=sum(r.get('cpu_trials',0) for r in records),workspace=dict(size=api.req.size_bytes,alignment=api.req.alignment_bytes),records=records,mutations=mutation_records)
    (args.evidence/'results.json').write_text(json.dumps(report,indent=2)+'\n');api.close()
    print(json.dumps({k:v for k,v in report.items() if k not in ['records','mutations']},indent=2))
    return int(not all(r['passed'] for r in records+mutation_records))

if __name__=='__main__':raise SystemExit(main())
