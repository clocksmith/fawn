"""Frozen M7 job admission, emitted-opcode execution and mutation controls."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from api_job_m7 import JobApi
from api_m5 import reflection_dict
from compute_oracle_m7 import Module, StepLimit, BindingAdmissionError
from fixtures_m7 import positives, negatives
from semantic_m5 import reflection_bytes
from spirv_oracle_base import ExecutionError, StructuralError, OracleLimit


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data,indent=2,sort_keys=True)+'\n',encoding='utf-8')


def execute_vectors(module: Module, expected: dict) -> dict:
    readonly={(b['group'],b['binding']) for b in expected['bindings'] if b['kind']==1 and b['access']==1}
    count=unfinished=steps=0
    opcodes=set()
    for vector in expected['vectors']:
        actual={k:list(v) for k,v in vector['initial'].items()}
        uniforms={k:bytes(v) for k,v in vector['uniforms'].items()}
        try:
            trace=module.execute(actual,vector['inputs'],readonly,uniforms,
                                 step_limit=10000 if expected.get('unfinished') else 1000000)
        except StepLimit:
            if not expected.get('unfinished'):
                raise
            unfinished+=1
            continue
        if expected.get('unfinished'):
            raise ExecutionError('dynamically infinite case terminated')
        if actual!=vector['want']:
            raise ExecutionError(f'wrong raw resource effects: actual={actual}, wanted={vector["want"]}')
        if uniforms!=vector['uniforms']:
            raise ExecutionError('uniform source bytes modified')
        if 'accesses' in vector and trace!=vector['accesses']:
            raise ExecutionError(f'source access order/nonexecution differs: {trace} != {vector["accesses"]}')
        count+=1;steps+=module.last_steps;opcodes.update(module.last_opcodes)
    return dict(cpu_finite_invocations=count,unfinished_invocations=unfinished,
                instruction_steps=steps,executed_opcodes=sorted(opcodes))


def controls(compiled: dict, validator: Path, directory: Path) -> list:
    records=[]
    def detect(name: str, case: str, edit) -> None:
        if case not in compiled:
            records.append(dict(case=name,passed=False,reason='source case not qualified'));return
        words,expected=compiled[case]
        original=Module(words);original.interface(expected)
        changed=edit(list(words),original)
        if changed is None or changed==words:
            records.append(dict(case=name,passed=False,reason='mutation not applied'));return
        path=directory/(name+'.spv')
        path.write_bytes(struct.pack('<'+'I'*len(changed),*changed))
        validation=subprocess.run([str(validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,check=False)
        if validation.returncode:
            records.append(dict(case=name,passed=True,detection='target_validator',reason=validation.stderr.strip()));return
        try:
            module=Module(changed);module.interface(expected);execute_vectors(module,expected)
        except OracleLimit as exc:
            records.append(dict(case=name,passed=False,detection='oracle_limit_not_detection',reason=str(exc)))
        except (ExecutionError,StructuralError,ValueError,TypeError,KeyError,IndexError) as exc:
            records.append(dict(case=name,passed=True,detection='instruction_execution',reason=str(exc)))
        else:
            records.append(dict(case=name,passed=False,detection='escaped'))
    def change_update(words: list, module: Module) -> list | None:
        continuing={a[1] for op,a,o in module.instructions if op==246}
        block=None
        for op,a,o in module.instructions:
            if op==248:block=a[0]
            if block in continuing and op==128 and module.defs.get(a[2],(None,))[0]==61 and module.constants.get(a[3])==1:
                # Missed update creates an unfinished loop, not a host oracle
                # that assumes finite source trip count.
                zero=next((i for i,v in module.constants.items() if v==0 and module.defs[i][1][0]==a[0]),None)
                if zero is not None:words[o+4]=zero;return words
        return None
    detect('wrong_loop_update_frequency','for_zero_one_many',change_update)
    def force_true_condition(words: list,module: Module,offset: int) -> list:
        # Preserve the conditional CFG so eager-RHS/guard controls exercise the
        # instruction interpreter rather than merely breaking structured SPIR-V.
        true_id=next((i for i,v in module.constants.items() if v is True),None)
        if true_id is None:
            bool_id=next(i for i,typ in module.types.items() if typ==(20,()))
            true_id=words[3];words[3]+=1
            words[offset+1]=true_id
            declaration=next(o for op,a,o in module.instructions if op==54)
            words[declaration:declaration]=[(3<<16)|41,bool_id,true_id]
        else:words[offset+1]=true_id
        return words
    def force_rhs(words: list, module: Module) -> list | None:
        for op,a,o in module.instructions:
            if op==250 and a[0] in module.constants and module.constants[a[0]] is False:
                return force_true_condition(words,module,o)
        return None
    detect('eager_short_circuit_rhs','short_circuit_and_false',force_rhs)
    def stale(words: list,module: Module) -> list | None:
        seven=next((i for i,v in module.constants.items() if v==7),None)
        for op,a,o in reversed(module.instructions):
            if op==61 and a[2] in module.variables and module.variables[a[2]][1]==7 and seven is not None:
                words[o]=(4<<16)|83;words[o+3]=seven;return words
        return None
    detect('stale_mutable_local','mutable_snapshot',stale)
    def no_zero(words: list,module: Module) -> list | None:
        stores=[(a,o) for op,a,o in module.instructions
                if op==62 and a[0] in module.variables
                and module.variables[a[0]][1]==7 and module.constants.get(a[1])==0]
        if stores:
            # The body declaration is last, after the for initializer. Remove
            # that dynamic zero store, not merely the loop counter initializer.
            _,offset=stores[-1];words[offset:offset+3]=[1<<16]*3;return words
        return None
    detect('missing_declaration_zero_initialization','declaration_zero_each_iteration',no_zero)
    def bad_merge(words: list,module: Module) -> list | None:
        for op,a,o in module.instructions:
            if op==246:words[o+1]=a[1];return words
        return None
    detect('wrong_loop_merge','loop_continuing_break_if',bad_merge)
    def wrong_order(words: list,module: Module) -> list | None:
        loop_index=next((i for i,(op,a,o) in enumerate(module.instructions) if op==246),None)
        if loop_index is None:return None
        continuing=module.instructions[loop_index][1][1]
        test_branch=next((a for op,a,o in module.instructions[loop_index+1:] if op==250),None)
        if test_branch is None:return None
        body=test_branch[1]
        labels={a[0]:o for op,a,o in module.instructions if op==248}
        if body not in labels or continuing not in labels:return None
        begin=labels[continuing]+2
        end=next((o for op,a,o in module.instructions if o>=begin and op in {249,250,253}),None)
        if end is None or end==begin:return None
        moved=words[begin:end]
        # Move actual load/add/store update ahead of the body while retaining
        # exactly one valid continue backedge and all result identities.
        del words[begin:end]
        insertion=labels[body]+2
        if insertion>begin:insertion-=end-begin
        words[insertion:insertion]=moved
        return words
    detect('wrong_loop_body_update_order','for_continue_updates',wrong_order)
    def bad_phi(words: list,module: Module) -> list | None:
        for op,a,o in module.instructions:
            if op==245 and len(a)>=6:words[o+6]=a[3];return words
        return None
    detect('wrong_actual_phi_predecessor','nested_logical_postguard_phi',bad_phi)
    def no_guard(words: list,module: Module) -> list | None:
        guards={a[1] for op,a,o in module.instructions if op==176 and module.defs.get(a[3],(None,))[0]==68}
        for op,a,o in module.instructions:
            if op==250 and a[0] in guards:
                return force_true_condition(words,module,o)
        return None
    detect('missing_independent_robust_guard','compound_independent_robustness',no_guard)
    def wrong_return(words: list,module: Module) -> list | None:
        continuing=next((a[1] for op,a,o in module.instructions if op==246),None)
        for op,a,o in module.instructions:
            if op==253 and continuing is not None:
                words[o:o+1]=[(2<<16)|249,continuing];return words
        return None
    detect('return_runs_continuing','return_bypasses_continuing',wrong_return)
    return records


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['library','validator','out']:
        p.add_argument('--'+name,type=Path,required=True,help='Explicit '+name+' path.')
    p.add_argument('--smoke',action='store_true',help='Only four decisive sources; no full acceptance claim.')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    modules=a.out/'modules';modules.mkdir();sources=a.out/'sources';sources.mkdir()
    api=JobApi(a.library);records=[];compiled={}
    pos=positives();neg=negatives()
    if a.smoke:
        pos=[c for c in pos if c[0] in {'mutable_snapshot','for_zero_one_many','loop_continuing_break_if','short_circuit_and_false'}];neg=[]
    for name,source,expected in pos:
        (sources/(name+'.wgsl')).write_bytes(source)
        status,result,words,errors=api.call(source)
        row=dict(case=name,source_sha256=sha(source),status=status,diagnostic=result.diagnostic.message.decode(errors='replace'))
        if status:errors.append('contract-admitted source rejected')
        else:
            if bytes(result.reflection)!=reflection_bytes(expected):errors.append('reflection differs including unused bytes: '+repr(reflection_dict(result.reflection)))
            raw=struct.pack('<'+'I'*len(words),*words);path=modules/(name+'.spv');path.write_bytes(raw)
            row.update(output_sha256=sha(raw),word_count=len(words))
            val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,check=False)
            if val.returncode:errors.append('Vulkan1.1 validation: '+val.stderr)
            try:
                module=Module(words);row['structure']=module.interface(expected)
                if expected['require_loop'] and 246 not in row['structure']['opcodes']:raise StructuralError('required actual LoopMerge absent')
                if expected['require_function_memory'] and not any(sc==7 for _,sc,_ in module.variables.values()):raise StructuralError('mutable Function OpVariable absent')
                row.update(execute_vectors(module,expected));compiled[name]=(words,expected)
            except (ExecutionError,StructuralError,OracleLimit,AssertionError,KeyError,IndexError,ValueError,TypeError) as exc:
                errors.append(type(exc).__name__+': '+str(exc))
        row.update(passed=not errors,errors=errors)
        records.append(row)
        print(json.dumps(row) if errors else 'PASS '+name,flush=True)
        write_json(a.out/'records.json',records)
    for name,source,wanted in neg:
        (sources/(name+'.wgsl')).write_bytes(source)
        status,result,_,errors=api.call(source)
        admitted_statuses=wanted if isinstance(wanted,tuple) else (wanted,)
        if status not in admitted_statuses:errors.append(f'expected status {wanted}, received {status}')
        records.append(dict(case=name,source_sha256=sha(source),status=status,expected_status=wanted,diagnostic=result.diagnostic.message.decode(errors='replace'),offset=result.diagnostic.offset,passed=not errors,errors=errors))
        if errors:print(json.dumps(records[-1]),flush=True)
        write_json(a.out/'records.json',records)
    mutations=[] if a.smoke else controls(compiled,a.validator,modules)
    report=dict(positive_count=len(pos),negative_count=len(neg),records=records,mutations=mutations,checks=len(records)+len(mutations),passed=sum(r['passed'] for r in records+mutations),cpu_finite_invocations=sum(r.get('cpu_finite_invocations',0) for r in records),unfinished_invocations=sum(r.get('unfinished_invocations',0) for r in records),physical_execution=False,runtime_adoption=False)
    write_json(a.out/'results.json',report);api.close()
    print(json.dumps({k:v for k,v in report.items() if k not in {'records','mutations'}},indent=2))
    return int(report['checks']!=report['passed'])

if __name__=='__main__':raise SystemExit(main())
