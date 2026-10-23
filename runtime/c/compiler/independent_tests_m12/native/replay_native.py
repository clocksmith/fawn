#!/usr/bin/env python3
"""Portable source-bound native replay. Default verifies paths only, never builds."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def check(path,digest):
    if sha(path)!=digest:raise ValueError('Identity mismatch: '+str(path))

def save(path,value):
    path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')

def verify_seal(root,seal):
    count=0
    for line in seal.read_text().splitlines():
        digest,name=line.split(None,1);name=name.lstrip('*');rel=Path(name)
        if rel.is_absolute() or '..' in rel.parts:raise ValueError('Unsafe seal path')
        path=(root/rel).resolve()
        if not path.is_relative_to(root):raise ValueError('Escaping seal path')
        check(path,digest);count+=1
    return count

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('repo','cases','owner','manifest','build-root','out','validator'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--manifest-sha256',required=True)
    parser.add_argument('--validator-sha256',required=True)
    parser.add_argument('--target-env',choices=('vulkan1.1',),default='vulkan1.1')
    parser.add_argument('--build-receipt',type=Path)
    parser.add_argument('--stage',choices=('build','audit','retained','numeric','sanitize','normal'),required=True)
    parser.add_argument('--execute',action='store_true',help='Explicitly execute this single stage after resource coordination; default is verification only')
    args=parser.parse_args()
    for name in ('repo','cases','owner','manifest','build_root','out','validator'):
        setattr(args,name,getattr(args,name).resolve())
    if args.build_receipt:args.build_receipt=args.build_receipt.resolve()
    if args.out.exists():raise ValueError('Use a new --out directory')
    roots=(HERE,args.repo,args.cases,args.owner,args.manifest.parent)
    for path in (*roots,args.out,args.build_root,args.validator):
        if any(c in str(path) for c in "'\"\\\n\r"):raise ValueError('Code-bearing path characters unsupported')
    for dest in (args.out,args.build_root):
        if any(dest.is_relative_to(root) or root.is_relative_to(dest) for root in roots):raise ValueError('Output/input overlap')
    if args.out.is_relative_to(args.build_root) or args.build_root.is_relative_to(args.out):raise ValueError('Separate build and receipt directories required')
    count=verify_seal(HERE,HERE/'SOURCE_SEAL.sha256')
    deps=json.loads((HERE/'DEPENDENCIES.json').read_text())
    for name,digest in deps['historical_files'].items():check(args.repo/name,digest)
    check(args.repo/deps['contract_relative'],deps['contract_sha256'])
    if args.validator_sha256!=deps['approved_validator']['sha256']:raise ValueError('Validator is not this accepted approved identity')
    check(args.validator,args.validator_sha256)
    check(args.manifest,args.manifest_sha256)
    native=HERE/'originals/native';correction=HERE/'originals/correction';normal=HERE/'originals/normal'
    for label,directory in [('native',native),('correction',correction),('normal',normal)]:
        check(directory/'SOURCE_SEAL.sha256',deps['original_'+('native_source' if label=='native' else label)+'_seal_sha256'])
        verify_seal(directory,directory/'SOURCE_SEAL.sha256')
    args.native=args.repo/deps['m11_native_relative']
    original=load('portable_original_native',native/'run_native_m12.py')
    inputs,pins,identities=original.immutable_inputs(args)
    builder=load('portable_accepted_builder',args.native/'build_m11.py')
    frozen=args.repo/'runtime/c/compiler/independent_tests_m10/native/frozen_headers'
    entries=builder.verify(args.owner,args.manifest,frozen)
    args.out.mkdir(parents=True,exist_ok=False)
    relocated=args.out/'relocated';relocated.mkdir()
    source_front=native/'run_native_m12.py';front=source_front;changes=[]
    if args.stage in ('numeric','sanitize'):
        source_record=json.loads((correction/'CORRECTION.json').read_text())
        updated=dict(source_record)
        mapping={'authoritative_correction':HERE/'proofs/EXACT_EVALUATOR_CORRECTIONS.json','scalar_adjudication':HERE/'proofs/SCALAR_ADJUDICATION.json','original_runner':source_front,'original_native_seal':native/'SOURCE_SEAL.sha256','original_native_first_failure':HERE/'proofs/ORIGINAL_NATIVE_FIRST_FAILURE.json'}
        for key,path in mapping.items():
            check(path,source_record[key+'_sha256'])
            updated[key]=str(path)
            changes.append({'kind':'correction_data_path','field':key,'old':source_record[key],'new':str(path),'content_sha256_unchanged':source_record[key+'_sha256']})
        record=relocated/'CORRECTION.json';save(record,updated)
        source_front=correction/'run_corrected_native_m12.py';text=source_front.read_text()
        old=sha(correction/'CORRECTION.json');new=sha(record)
        if text.count(old)!=1:raise ValueError('Unexpected correction spec hash literal')
        text=text.replace(old,new)
        front=relocated/source_front.name;front.write_text(text)
        changes.append({'kind':'relocated_correction_json_hash','old':old,'new':new,'semantic_fields_changed':False})
        official=json.loads(mapping['authoritative_correction'].read_text())
        row=next(r for r in official['corrections'] if r['id']==source_record['id'])
        assert row['source_sha256']==source_record['source_sha256'] and row['corrected_expected_status']==source_record['new_status']==4
        check(args.cases/source_record['source'],source_record['source_sha256'])
    elif args.stage=='normal':
        source_front=normal/'run_normal_profile.py';text=source_front.read_text()
        old="/tmp/doe-m12-native-20261023/run_native_m12.py";new=str(native/'run_native_m12.py')
        if text.count(old)!=1:raise ValueError('Unexpected normal original-runner literal')
        text=text.replace(old,new)
        front=relocated/source_front.name;front.write_text(text)
        changes.append({'kind':'normal_original_runner_path','old':old,'new':new,'expectations_changed':False})
    command=[sys.executable,str(front),'--repo',str(args.repo),'--cases',str(args.cases),'--native',str(args.native),'--owner',str(args.owner),'--manifest',str(args.manifest),'--manifest-sha256',args.manifest_sha256,'--build-root',str(args.build_root),'--out',str(args.out/'gate')]
    if args.stage!='normal':
        command+=['--stage',args.stage,'--execute']
        if args.build_receipt:command+=['--build-receipt',str(args.build_receipt)]
    if args.execute and args.stage not in ('build','normal') and args.build_receipt is None:raise ValueError('Execute nonbuild stages with explicit --build-receipt')
    recipe={'status':'PREPARED_NOT_EXECUTED','package_source_seal_sha256':sha(HERE/'SOURCE_SEAL.sha256'),'package_files':count,'source_manifest_sha256':args.manifest_sha256,'source_entries':len(entries),'accepted_historical_commit':deps['accepted_history_commit'],'historical_files_verified':len(deps['historical_files']),'case_seal_sha256':inputs['case_seal_sha256'],'native_seals_unchanged':True,'stage':args.stage,'command':command,'source_front_door':str(source_front),'source_front_door_sha256':sha(source_front),'effective_front_door':str(front),'effective_front_door_sha256':sha(front),'relocations':changes,'validator':{'path':str(args.validator),'sha256':args.validator_sha256,'target_env':args.target_env,'executed_by_native_front_door':False,'consumer_contract':'Native stages emit TARGET_MANIFEST.json; parent portable semantic runner consumes it with this approved validator and --target-env vulkan1.1.'},'compiler_builds_or_tests_run':0,'wall_timeout':None,'profile_distinctions':'build/audit uses original strict-PIC builder; normal uses exact Makefile CFLAGS/non-PIC, only .su/.ci instrumentation; sanitizer uses original separately sealed recipes.'}
    save(args.out/'PLAN.json',recipe)
    save(args.out/'PATH_VERIFICATION.json',dict(status='PASS_PATH_AND_SOURCE_IDENTITIES_ONLY',source_entries=len(entries),historical_files=len(deps['historical_files']),original_identities=identities,compiler_builds_or_tests_run=0))
    if not args.execute:
        print('PREPARED_NOT_EXECUTED '+str(args.out));return 0
    # The selected original runner owns command/progress receipts and unfinished
    # gate classification. No outer wall timeout is introduced here.
    result=subprocess.run(command)
    save(args.out/'EXECUTION.json',dict(returncode=result.returncode,stage=args.stage,source_manifest_sha256=args.manifest_sha256,receipt_directory=str(args.out/'gate'),wall_timeout=None,scope='Selected native gate only; inspect original RESULT.json for verdict.'))
    return result.returncode

if __name__=='__main__':raise SystemExit(main())
