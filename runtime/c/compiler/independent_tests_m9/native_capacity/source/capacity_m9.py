"""Independent M9 job storage/format tests, sealed before candidate inspection."""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from api_job_m7 import JobApi, Capacities, capacities, cap_dict, diagnostic
from api_m5 import Requirements

ENTRY = '@compute @workgroup_size(1) fn main()'
OK, INPUT_ERROR, PARSE_ERROR, UNSUPPORTED, SEMANTIC_ERROR, OUTPUT_TOO_SMALL, ABI_MISMATCH = range(7)


def tokens(source: str) -> int:
    # Fixtures deliberately use no comments or multi-token numeric operators.
    return len(re.findall(r'\b[A-Za-z_][A-Za-z_0-9]*\b|[0-9]+[uif]?|->|&&|[^\s]', source))


def case(name: str, source: str, expressions: int, statements: int,
         status: int = OK, budget: str = '', **changes: int) -> dict:
    caps = dict(source_bytes=len(source.encode()), tokens=tokens(source),
                statements=statements, expressions=expressions, words=65536,
                expression_depth=64, control_depth=32)
    caps.update(changes)
    return dict(name=name, source=source, capacities=caps, status=status, budget=budget)


def fixtures() -> list[dict]:
    rows = []
    for count in (0, 1, 17, 127, 341):
        source = ''.join(f'fn h{i}(){{}}' for i in range(count)) + ENTRY + '{}'
        rows += [case(f'empty_helpers_{count}', source, 0, 0),
                 case(f'empty_helpers_{count}_token_short', source, 0, 0,
                      INPUT_ERROR, 'token', tokens=tokens(source)-1)]
    for count in (1, 17, 128, 512, 2048):
        formals = ','.join(f'p{i}:u32' for i in range(count))
        source = f'fn f({formals}){{}}' + ENTRY + '{}'
        rows += [case(f'zero_es_formals_{count}', source, 0, 0),
                 case(f'zero_es_formals_{count}_token_short', source, 0, 0,
                      INPUT_ERROR, 'token', tokens=tokens(source)-1)]
    rows.append(case('zero_function_records', ENTRY+'{}', 0, 0, INPUT_ERROR, '', tokens=0))
    for source in ('fn', 'fn f(', 'fn f(a:', 'fn f(a:u32'):
        rows.append(case('malformed_prefix_'+str(len(source)), source, 0, 0,
                         PARSE_ERROR, '', tokens=32))
    base = 'fn f(a:u32,b:u32)->u32{return a;}'
    rows += [case('zero_call', 'fn f(){}'+ENTRY+'{f();}', 1, 1),
             case('zero_call_short_e', 'fn f(){}'+ENTRY+'{f();}', 0, 1, INPUT_ERROR, 'expression'),
             case('zero_call_short_s', 'fn f(){}'+ENTRY+'{f();}', 1, 0, INPUT_ERROR, 'statement'),
             case('simple_args', base+ENTRY+'{f(1u,2u);}', 4, 2),
             case('simple_args_short_e', base+ENTRY+'{f(1u,2u);}', 3, 2, INPUT_ERROR, 'expression'),
             case('nested_args', base+'fn g(x:u32)->u32{return x;}'+ENTRY+'{f(g(1u),g(2u));}', 7, 3),
             case('dead_args', base+ENTRY+'{return;f(1u,2u);}', 4, 3),
             case('skipped_args', 'fn f(x:bool)->bool{return x;}'+ENTRY+'{let x=false&&f(true);}', 5, 2),
             case('physical_alias_before_entry', 'fn f(x:u32)->u32{return x;}'+
                  '@group(0) @binding(0) var<storage,read_write> out:array<u32>;'+
                  '@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3<u32>)'
                  '{let i=gid.x;out[i]=f(i);}', 16, 4)]
    source = 'fn f(a:u32){}'+ENTRY+'{'+''.join(f'f({i}u);' for i in range(128))+'}'
    rows += [case('repeated_tight_args', source, 256, 128),
             case('repeated_tight_args_short_e', source, 255, 128, INPUT_ERROR, 'expression')]
    # No graph walk consumes more call slots on repeated validation of skipped
    # source; this syntactically small source has independently counted nodes.
    rows.append(case('nested_skipped_dead', 'fn f(x:bool)->bool{return x;}'+ENTRY+
                     '{return;let q=false&&f(f(true));}', 6, 3))
    return rows


def format_fixture(count: int, call: bool) -> dict:
    formals = ','.join(f'p{i}:u32' for i in range(count))
    body = 'f('+','.join('0u' for _ in range(count))+');' if call else ''
    source = f'fn f({formals}){{}}'+ENTRY+'{'+body+'}'
    expected = UNSUPPORTED if count>(65531 if call else 65532) else OK
    return case(('call_' if call else 'type_')+str(count), source,
                count+1 if call else 0, 1 if call else 0, expected,
                'SPIR-V' if expected else '', words=1048576)


def all_fixtures() -> list[dict]:
    return fixtures()+[format_fixture(65532,False), format_fixture(65533,False),
                       format_fixture(65531,True), format_fixture(65532,True)]


def manifest() -> list[dict]:
    return [{k:v for k,v in row.items() if k!='source'} |
            dict(source_bytes=len(row['source'].encode()),
                 source_sha256=hashlib.sha256(row['source'].encode()).hexdigest())
            for row in all_fixtures()]


def validate(words: list[int], validator: Path, large: bool) -> dict:
    module = struct.pack('<'+'I'*len(words), *words)
    command = [str(validator),'--target-env','vulkan1.1']
    default = subprocess.run(command, input=module, capture_output=True, timeout=120)
    if large:
        command += ['--max-function-args','65532']
        selected = subprocess.run(command, input=module, capture_output=True, timeout=120)
    else:
        selected = default
    return dict(command=command, returncode=selected.returncode,
                stderr=selected.stderr.decode(errors='replace'),
                default_returncode=default.returncode,
                default_stderr=default.stderr.decode(errors='replace'),
                sha256=hashlib.sha256(module).hexdigest(), words=len(words))


def inspect_format(words: list[int], row: dict) -> list[str]:
    errors=[]; found=[]; parameters=set(); names=set(); at=5
    while at<len(words):
        width,op=words[at]>>16,words[at]&65535
        if width<1 or at+width>len(words):
            return ['invalid instruction framing']
        if op==55: parameters.add(words[at+2])
        if op==5: names.add(words[at+1])
        if op==33 and row['name'].startswith('type_'): found.append(width)
        if op==57 and row['name'].startswith('call_'): found.append(width)
        at+=width
    if parameters&names: errors.append('formal parameter OpName violates 4P word ledger')
    if row['name'] in ('type_65532','call_65531') and 65535 not in found:
        errors.append('positive format edge did not serialize exact 65535-word instruction')
    caps=row['capacities']; i=1+4*caps['expressions']+8*caps['statements']; f=caps['tokens']//6; p=caps['tokens']//4
    if words[3]>256+32*i+3*f+p: errors.append('target bound exceeds frozen B ledger')
    if len(words)>2048+256*i+32*f+4*p+caps['expressions']:
        errors.append('word count exceeds frozen whole-module ledger')
    return errors


def query_edges(library: Path) -> list[dict]:
    lib=C.CDLL(str(library)); query=lib.doe_compiler_job_requirements
    query.argtypes=[C.c_uint32,C.POINTER(Capacities),C.c_size_t,C.POINTER(Requirements),C.c_size_t]
    query.restype=C.c_int
    max_size=(1<<(8*C.sizeof(C.c_size_t)))-1
    base=dict(source_bytes=0,tokens=0,statements=0,expressions=0,words=0,expression_depth=1,control_depth=1)
    trials=[('zero_counts',{},OK),('max_source',{'source_bytes':0xfffffffe},OK),
            ('source_u32_reject',{'source_bytes':0xffffffff},INPUT_ERROR),
            ('expression_id_exact',{'expressions':33554429},OK),
            ('expression_id_one_past',{'expressions':33554430},INPUT_ERROR),
            ('statement_id_exact',{'statements':16777214},OK),
            ('statement_id_one_past',{'statements':16777215},INPUT_ERROR)]
    for field in ('tokens','statements','expressions','words'):
        trials += [(field+'_size_max',{field:max_size},INPUT_ERROR),
                   (field+'_multiply_overflow',{field:max_size//4+1},INPUT_ERROR)]
    for field in ('tokens','statements','expressions'):
        trials.append((field+'_u32_overflow',{field:1<<32},INPUT_ERROR))
    rows=[]
    for name,change,expected in trials:
        caps=Capacities(**(base|change)); req=Requirements(); C.memset(C.byref(req),0xa5,C.sizeof(req)); before=bytes(req)
        status=query(3,C.byref(caps),C.sizeof(caps),C.byref(req),C.sizeof(req))
        errors=[]
        if status!=expected: errors.append(f'status {status}, expected {expected}')
        if status and bytes(req)!=before: errors.append('failure modified requirements')
        if not status and (not req.size_bytes or req.alignment_bytes>C.alignment(C.c_longdouble)): errors.append('invalid successful layout')
        rows.append(dict(name=name,status=status,expected=expected,errors=errors,size=req.size_bytes if not status else None))
    return rows


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path)
    parser.add_argument('--validator',type=Path)
    parser.add_argument('--out',type=Path)
    parser.add_argument('--manifest',action='store_true')
    parser.add_argument('--only-small',action='store_true')
    args=parser.parse_args()
    if args.manifest:
        print(json.dumps(manifest(),indent=2,sort_keys=True)); return 0
    if not args.library or not args.out or not args.validator: parser.error('library, validator, out required')
    args.out.mkdir(parents=True,exist_ok=False)
    rows=query_edges(args.library.resolve())
    for row in fixtures() if args.only_small else all_fixtures():
        record={k:v for k,v in row.items() if k!='source'}; record['errors']=[]
        source=row['source'].encode(); record['source_sha256']=hashlib.sha256(source).hexdigest()
        try:
            api=JobApi(args.library,Capacities(**row['capacities']),extra_workspace=64)
            status,result,words,errors=api.call(source)
            record.update(status=status,diagnostic=diagnostic(result),workspace=api.req.size_bytes)
            record['errors']+=errors
            if status!=row['status']: record['errors'].append(f"status {status}, expected {row['status']}")
            if status and row['budget'] and row['budget'].lower() not in record['diagnostic']['message'].lower(): record['errors'].append('missing named budget/format diagnostic')
            if row['status']==UNSUPPORTED and status==UNSUPPORTED:
                # The source location must identify the oversized declaration or call.
                off=result.diagnostic.offset; last=source.rfind(b'f(')
                if row['name'].startswith('call_') and not last<=off<last+2: record['errors'].append('format call diagnostic not at actual call')
                if row['name'].startswith('type_') and not 0<=off<5: record['errors'].append('format declaration diagnostic not at actual declaration')
                if not re.search(r'word.{0,3}count',record['diagnostic']['message'],re.I): record['errors'].append('missing instruction-word-count explanation')
            if not status:
                record['validation']=validate(words,args.validator,row['name'].startswith(('type_','call_')))
                if record['validation']['returncode']: record['errors'].append('Vulkan1.1 validation failed')
                record['errors']+=inspect_format(words,row)
            if not status and not row['name'].startswith(('type_','call_')):
                # Exact and one-short caller word capacity, same workspace and no reset.
                for capacity,expected in ((len(words),OK),(len(words)-1,OUTPUT_TOO_SMALL),(0,OUTPUT_TOO_SMALL)):
                    st,r,w,es=api.call(source,capacity); record['errors']+=es
                    if st!=expected: record['errors'].append(f'caller word edge {capacity}: {st} != {expected}')
                # Staging budget is independently enforced despite spare physical bytes.
                caps=dict(row['capacities'],words=len(words)-1)
                short=JobApi(args.library,Capacities(**caps),extra_workspace=4096)
                st,r,w,es=short.call(source,len(words)); record['errors']+=es; short.close()
                if st!=OUTPUT_TOO_SMALL: record['errors'].append('short staging budget not enforced')
            if not row['name'].startswith(('type_','call_')):
                st,r,w,es=api.call(source); record['errors']+=es
                if st!=status: record['errors'].append('reuse changed status')
            api.close()
        except (OSError,ValueError,AssertionError,subprocess.TimeoutExpired) as error:
            record['errors'].append(str(error))
        rows.append(record)
        (args.out/'results.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
        if record['errors'] and not (args.out/'FIRST_FAILURE.json').exists():
            (args.out/'FIRST_FAILURE.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
        print('FAIL' if record['errors'] else 'PASS',row['name'],record['errors'],flush=True)
    failures=[row for row in rows if row['errors']]
    (args.out/'summary.json').write_text(json.dumps(dict(status='FAIL' if failures else 'PASS',checks=len(rows),failures=len(failures)),indent=2)+'\n')
    return bool(failures)

if __name__=='__main__':
    raise SystemExit(main())
