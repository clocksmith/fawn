"""M6 independent admission, reflection, Vulkan1.1 validation and opcode execution."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from api_m5 import Api, reflection_dict
from semantic_m5 import reflection_bytes
from compute_oracle_m6 import Module, BindingAdmissionError
from spirv_oracle_base import ExecutionError, StructuralError, OracleLimit
from fixtures_m6 import positives, negatives


def execute_vectors(module: Module, expected: dict) -> tuple:
    readonly={(b['group'],b['binding']) for b in expected['bindings'] if b['kind']==1 and b['access']==1}
    count=synthetic=dispatch_count=0
    for vector in expected['vectors']:
        actual={k:list(v) for k,v in vector['initial'].items()}
        uniforms={k:bytes(v) for k,v in vector['uniforms'].items()}
        inputs=vector.get('dispatch',[vector['inputs']])
        for builtin_values in inputs:
            module.execute(actual,builtin_values,readonly,uniforms)
            count+=1
            synthetic+=int(vector['synthetic'])
        dispatch_count+=int('dispatch' in vector)
        if actual!=vector['want']:
            raise ExecutionError(f"wrong buffers: inputs={inputs[:2]} actual={actual} expected={vector['want']}")
        if uniforms!=vector['uniforms']:
            raise ExecutionError('uniform bytes mutated')
    return count,synthetic,dispatch_count


def controls(compiled: dict) -> list:
    records=[]
    def detect(name: str, words: list, expected: dict, edit) -> None:
        changed=list(words);edit(changed)
        assert changed!=words, name
        try:
            module=Module(changed);module.interface(expected);execute_vectors(module,expected)
        except OracleLimit as exc:
            records.append(dict(case=name,passed=False,classification='ORACLE_LIMIT_NOT_PASS',reason=str(exc)))
        except (ExecutionError,StructuralError,KeyError,IndexError,TypeError,ValueError) as exc:
            records.append(dict(case=name,passed=True,classification='DEFECT_DETECTED',reason=str(exc)))
        else:
            records.append(dict(case=name,passed=False,classification='DEFECT_ESCAPED'))
    name='all_builtin_components_coherent_dispatch'
    if name in compiled:
        words,expected=compiled[name];m=Module(words);m.interface(expected)
        for op,a,offset in m.instructions:
            if op==71 and a[1:]==(11,28):
                detect('wrong_builtin_identity',words,expected,lambda w,o=offset:w.__setitem__(o+3,26));break
        for op,a,offset in m.instructions:
            if op==81 and a[3]==0:
                detect('wrong_builtin_component',words,expected,lambda w,o=offset:w.__setitem__(o+4,1));break
    name='indirect_wrap_each_binding'
    if name in compiled:
        words,expected=compiled[name];m=Module(words);m.interface(expected)
        roots={key:rid for rid,key in m.resources.items()}
        for op,a,offset in m.instructions:
            if op==71 and a[0]==roots[(0,1)] and a[1]==33:
                detect('wrong_resource_identity',words,expected,lambda w,o=offset:w.__setitem__(o+3,19));break
        for op,a,offset in m.instructions:
            if op==65 and a[2]==roots[(0,0)]:
                detect('wrong_load_resource',words,expected,lambda w,o=offset:w.__setitem__(o+3,roots[(0,1)]));break
        guards={a[1]:(a,offset) for op,a,offset in m.instructions if op==176 and m.defs.get(a[3],(None,))[0]==68}
        branches=[(a,offset) for op,a,offset in m.instructions if op==250 and a[0] in guards]
        if branches:
            a,offset=branches[0]
            def force(w: list, offset: int=offset, target: int=a[1]) -> None:
                w[offset:offset+4]=[(2<<16)|249,target,1<<16,1<<16]
            detect('omitted_nested_guard',words,expected,force)
        # Last guarded storage write: a wrong false edge suppresses valid stores.
        if branches:
            a,offset=branches[-1]
            def suppress(w: list, offset: int=offset, target: int=a[2]) -> None:
                w[offset:offset+4]=[(2<<16)|249,target,1<<16,1<<16]
            detect('suppressed_valid_post_oob_store',words,expected,suppress)
        for result,(a,offset) in guards.items():
            index_def=m.defs.get(a[2]);
            if index_def is None or index_def[0]!=128:continue
            plus=index_def[1]
            mul=m.defs.get(plus[2])
            if mul is not None and mul[0]==132:
                detect('guard_before_wrapped_product',words,expected,lambda w,o=offset,r=mul[1][2]:w.__setitem__(o+3,r));break
        one=next((rid for rid,v in m.constants.items() if v==1 and m.types.get(m.defs[rid][1][0])==(21,(32,0))),None)
        for op,a,offset in m.instructions:
            if op==245 and one is not None:
                zero_position=next((i for i in range(2,len(a),2) if m.constants.get(a[i])==0),None)
                if zero_position is not None:
                    detect('oob_token_wrong_default',words,expected,lambda w,o=offset,i=zero_position:w.__setitem__(o+1+i,one));break
    name='condition_nested_2'
    if name not in compiled:
        name=next((n for n in compiled if n.startswith('condition_nested_')),None)
    if name:
        words,expected=compiled[name];m=Module(words);m.interface(expected)
        for op,a,offset in m.instructions:
            if op==245 and len(a)>=6:
                detect('wrong_phi_predecessor',words,expected,lambda w,o=offset,r=a[3]:w.__setitem__(o+6,r));break
    required={'wrong_builtin_identity','wrong_builtin_component','wrong_resource_identity','wrong_load_resource',
              'omitted_nested_guard','suppressed_valid_post_oob_store','guard_before_wrapped_product',
              'oob_token_wrong_default','wrong_phi_predecessor'}
    missing=required-{r['case'] for r in records}
    if missing:records.append(dict(case='mutation_coverage',passed=False,missing=sorted(missing)))
    return records


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['repo','library','validator','out']:
        p.add_argument('--'+name,type=Path,required=True,help='Explicit '+name+' path.')
    p.add_argument('--smoke',action='store_true',help='Run four decisive positives only, no retained campaign.')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    modules=a.out/'modules';modules.mkdir();sources=a.out/'sources';sources.mkdir()
    api=Api(a.library);records=[];compiled={}
    pos=positives(a.repo);neg=negatives()
    if a.smoke:
        pos=[c for c in pos if c[0] in {'indirect_wrap_each_binding','all_builtin_components_coherent_dispatch','real_gather','long_entry_five_unused_inputs'}];neg=[]
    for name,source,expected in pos:
        (sources/(name+'.wgsl')).write_bytes(source)
        status,result,words,errors=api.call(source)
        row=dict(case=name,source_sha256=hashlib.sha256(source).hexdigest(),status=status,
                 diagnostic=result.diagnostic.message.decode(errors='replace'))
        if status:errors.append('contract-admitted source rejected')
        else:
            if bytes(result.reflection)!=reflection_bytes(expected):
                errors.append('reflection differs including unused bytes: '+repr(reflection_dict(result.reflection)))
            raw=struct.pack('<'+'I'*len(words),*words);path=modules/(name+'.spv');path.write_bytes(raw)
            row['output_sha256']=hashlib.sha256(raw).hexdigest();row['word_count']=len(words)
            val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,timeout=15)
            if val.returncode:errors.append('Vulkan1.1 validation: '+val.stderr)
            try:
                m=Module(words);row['structure']=m.interface(expected)
                row['cpu_invocations'],row['synthetic_single_invocations'],row['coherent_dispatch_trials']=execute_vectors(m,expected)
                first=expected['vectors'][0]
                for key,minimum in m.uniform_minimums.items():
                    for length in [0,minimum-1]:
                        short=dict(first['uniforms']);short[key]=short[key][:length]
                        initial={k:list(v) for k,v in first['initial'].items()}
                        try:m.execute(initial,first['inputs'],set(),short)
                        except BindingAdmissionError:pass
                        else:raise ExecutionError('short uniform admitted')
                        if initial!=first['initial']:raise ExecutionError('short uniform admission touched storage')
                row['short_uniform_checks']=2*len(m.uniform_minimums)
                compiled[name]=(words,expected)
            except (ExecutionError,StructuralError,OracleLimit,AssertionError,KeyError,IndexError,ValueError,TypeError) as exc:
                errors.append(type(exc).__name__+': '+str(exc))
        records.append(dict(**row,passed=not errors,errors=errors))
        print(json.dumps(records[-1]) if errors else 'PASS '+name,flush=True)
    for name,source in neg:
        (sources/(name.replace('/','_')+'.wgsl')).write_bytes(source)
        status,result,_,errors=api.call(source)
        if not status:errors.append('excluded/malformed source accepted')
        records.append(dict(case=name,status=status,passed=not errors,errors=errors,
                            diagnostic=result.diagnostic.message.decode(errors='replace'),offset=result.diagnostic.offset,
                            source_sha256=hashlib.sha256(source).hexdigest()))
        if errors:print(json.dumps(records[-1]),flush=True)
        st,_,_,problems=api.call(pos[0][1])
        if st or problems:records.append(dict(case=name+'_reuse',passed=False,errors=problems+[str(st)]))
    mutations=[] if a.smoke else controls(compiled)
    report=dict(positive_count=len(pos),negative_count=len(neg),records=records,mutations=mutations,
                checks=len(records)+len(mutations),passed=sum(r['passed'] for r in records+mutations),
                cpu_invocations=sum(r.get('cpu_invocations',0) for r in records),
                synthetic_single_invocations=sum(r.get('synthetic_single_invocations',0) for r in records),
                coherent_dispatch_trials=sum(r.get('coherent_dispatch_trials',0) for r in records),
                workspace=dict(size=api.req.size_bytes,alignment=api.req.alignment_bytes),
                physical_execution=False,runtime_adoption=False)
    (a.out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');api.close()
    print(json.dumps({k:v for k,v in report.items() if k not in {'records','mutations'}},indent=2))
    return int(report['checks']!=report['passed'])

if __name__=='__main__':raise SystemExit(main())
