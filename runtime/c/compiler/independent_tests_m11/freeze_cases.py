from __future__ import annotations
import hashlib
import json
import struct
from pathlib import Path
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
REPO = Path('/workspace/shared/doe-c-migration-20261023')
FIX = HERE / 'frozen'
FIX.mkdir(exist_ok=True)
CASES = []

def digest(data):
    return hashlib.sha256(data).hexdigest()

def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')

ENTRY = ('@compute @workgroup_size(4) fn main('
         '@builtin(local_invocation_index) li:u32,'
         '@builtin(local_invocation_id) lid:vec3u,'
         '@builtin(global_invocation_id) gid:vec3u,'
         '@builtin(workgroup_id) wid:vec3u,'
         '@builtin(num_workgroups) nw:vec3u)')
RES = ('struct Params { x:u32, y:u32, z:u32, w:u32 };\n'
       '@group(0) @binding(0) var<uniform> params:Params;\n'
       '@group(0) @binding(1) var<storage,read> ro:array<u32>;\n'
       '@group(0) @binding(2) var<storage,read_write> rw:array<u32>;\n')

def graph(path=None, extra=()):
    edges = [['E','U0']]
    if path:
        edges += [[a,b] for a,b in zip(['R']+path, path+['N'])]
    else:
        edges += [['R','E']]
    edges += list(extra)
    return {'required':'R','nonuniform':'N','edges':edges,
            'description':'Explicit dependency graph edges point from dependent to dependency. U0 is uniform; N is intrinsic MayBeNonUniform. No numeric evaluation or author solver is used.'}

def add(name, body, status=0, prefix='', path=None, expected=None,
        obligation='', source=None, extra=()):
    src = source if source is not None else prefix + ENTRY + '{\n' + body + '\n}\n'
    filename = name + '.wgsl'
    (FIX / filename).write_text(src)
    item = {'id':name,'source':filename,'source_sha256':digest(src.encode()),
            'expected_status':status,'obligation':obligation}
    if status in (0,4) and (path is not None or 'workgroupBarrier' in src):
        item['uniformity_graph'] = graph(path,extra)
        item['graph_reaches_nonuniform'] = path is not None
        if status == 4 and path is not None:
            item['diagnostic'] = 'Original source builtin/helper call; uniform control cannot be established; output atomic.'
    if expected is not None:
        item['execution'] = expected
    CASES.append(item)

# Explicit expression dependency pairs; every syntactic operand remains represented.
exprs = [
 ('literal','true',False,''),('workgroup_id','wid.x == 0u',False,''),
 ('num_workgroups','nw.x > 0u',False,''),
 ('local_index','li == 0u',True,''),('local_id','lid.y == 0u',True,''),
 ('global_id','gid.x == 0u',True,''),
 ('uniform_fixed','params.x == 0u',False,RES),
 ('readonly_fixed','ro[0] == 0u',False,RES),
 ('readonly_uniform_index','ro[wid.x] == 0u',False,RES),
 ('readonly_varying_index','ro[li] == 0u',True,RES),
 ('mutable_fixed','rw[0] == 0u',True,RES),
 ('workgroup_fixed','shared == 0u',True,'var<workgroup> shared:u32;\n'),
 ('skipped_and_varying','false && li > 0u',True,''),
 ('skipped_or_varying','true || li > 0u',True,''),
 ('short_and_uniform','true && wid.x == 0u',False,''),
 ('short_or_uniform','false || nw.x > 0u',False,''),
 ('uniform_index_varying_cf','ro[0] == 0u',True,RES),
]
for name,expr,varying,prefix in exprs:
    body = f'if ({expr}) {{ workgroupBarrier(); }}'
    if name == 'uniform_index_varying_cf':
        body = f'if (li == 0u) {{ {body} }}'
    add('uniform_'+name,body,4 if varying else 0,prefix,
        ['barrier_cf','condition','source'] if varying else None,
        obligation='Dated WGSL 15.2.8 expression and source-control dependencies')

pairs = [
 ('whole_assignment_kills','var x=li; x=0u; if(x==0u){workgroupBarrier();}',False),
 ('let_snapshot_survives','var x=li; let y=x; x=0u; if(y==0u){workgroupBarrier();}',True),
 ('branch_equal_assignments','var x=0u; if(li==0u){x=1u;}else{x=1u;} if(x==1u){workgroupBarrier();}',True),
 ('branch_overwrite_after','var x=0u; if(li==0u){x=1u;} x=0u; if(x==0u){workgroupBarrier();}',False),
 ('if_reconverges','if(li==0u){let q=li;} workgroupBarrier();',False),
 ('if_return_no_reconvergence','if(li==0u){return;} workgroupBarrier();',True),
 ('uniform_return','if(wid.x==1u){return;} workgroupBarrier();',False),
 ('dead_suffix_excluded','return; if(li==0u){workgroupBarrier();}',False),
 ('constant_false_branch_retained','if(false){if(li==0u){workgroupBarrier();}}',True),
 ('single_array_partial_retains','var a=array<u32,1>(li); a[0]=0u; if(a[0]==0u){workgroupBarrier();}',True),
 ('array_whole_kills','var a=array<u32,1>(li); a=array<u32,1>(0u); if(a[0]==0u){workgroupBarrier();}',False),
 ('vector_partial_retains','var a=vec2u(li,0u); a.x=0u; if(a.y==0u){workgroupBarrier();}',True),
 ('vector_whole_kills','var a=vec2u(li,0u); a=vec2u(0u); if(a.y==0u){workgroupBarrier();}',False),
 ('partial_index_dependency','var a=array<u32,4>(); a[li]=0u; if(a[0]==0u){workgroupBarrier();}',True),
 ('read_in_varying_control','let x=0u; if(li==0u){if(x==0u){workgroupBarrier();}}',True),
 ('loop_divergent_reconverges','var x=li; while(x<4u){x=x+1u;} workgroupBarrier();',False),
 ('for_divergent_reconverges','for(var x=li;x<8u;x=x+4u){} workgroupBarrier();',False),
 ('loop_break_backedge','var x=0u; loop{workgroupBarrier(); if(x==li){break;} x=x+1u;}',True),
 ('loop_continue_barrier','var x=0u; loop{if(x>=4u){break;} x=x+1u; if(x==li){continue;} workgroupBarrier();}',True),
 ('loop_continue_reconverges','var x=0u; loop{if(x>=4u){break;} x=x+1u; if(x==li){continue;}} workgroupBarrier();',False),
 ('continuing_break_if','var x=0u; loop{workgroupBarrier(); continuing{x=x+1u; break if x>li;}}',True),
 ('continuing_uniform_break_if','var x=0u; loop{workgroupBarrier(); continuing{x=x+1u; break if x>2u;}}',False),
 ('loop_early_return','var x=0u; loop{if(x==2u){break;} if(li==0u){return;} x=x+1u;} workgroupBarrier();',True),
 ('loop_local_backedge','var x=0u; var y=0u; loop{if(x==2u){break;} if(y==0u){workgroupBarrier();} y=li; x=x+1u;}',True),
 ('loop_local_kill','var x=0u; var y=li; loop{if(x==2u){break;} y=0u; if(y==0u){workgroupBarrier();} x=x+1u;}',False),
 ('nested_loop_reconvergence','for(var i=0u;i<2u;i=i+1u){for(var j=li;j<4u;j=j+1u){} workgroupBarrier();}',False),
 ('nested_loop_varying_backedge','var i=0u; loop{if(i==2u){break;} var j=0u; loop{workgroupBarrier(); if(j==li){break;} j=j+1u;} i=i+1u;}',True),
 ('shadow_local_identity','var x=li; {let x=0u; if(x==0u){workgroupBarrier();}}',False),
 ('shadow_initializer_old_binding','var x=li; {let x=x; if(x==0u){workgroupBarrier();}}',True),
]
for name,body,varying in pairs:
    add('flow_'+name,body,4 if varying else 0,path=['required_control','reaching_value_or_cf','varying_origin'] if varying else None,
        obligation='Explicit reaching-definition/control graph; whole kill, partial retain, actual behavior channels and loop backedges')

helper = 'fn sync()->bool{workgroupBarrier(); return true;}\n'
helper_cases = [
 ('helper_direct','sync();',False,helper),
 ('helper_varying','if(li==0u){sync();}',True,helper),
 ('helper_skipped_and_uniform','let x=false && sync();',False,helper),
 ('helper_skipped_or_uniform','let x=true || sync();',False,helper),
 ('helper_skipped_and_varying_cf','if(li==0u){let x=false && sync();}',True,helper),
 ('helper_skipped_or_varying_cf','if(li==0u){let x=true || sync();}',True,helper),
 ('helper_runtime_short_varying','let x=li==0u && sync();',True,helper),
 ('helper_chain','a();',False,helper+'fn a(){b();} fn b(){sync();}\n'),
 ('helper_chain_varying','if(li==0u){a();}',True,helper+'fn a(){b();} fn b(){sync();}\n'),
 ('formal_required_uniform','f(wid.x);',False,'fn f(x:u32){if(x==0u){workgroupBarrier();}}\n'),
 ('formal_required_varying','f(li);',True,'fn f(x:u32){if(x==0u){workgroupBarrier();}}\n'),
 ('formal_return_uniform','if(f(0u,li)==0u){workgroupBarrier();}',False,'fn f(x:u32,y:u32)->u32{return x;}\n'),
 ('formal_return_varying','if(f(li,0u)==0u){workgroupBarrier();}',True,'fn f(x:u32,y:u32)->u32{return x;}\n'),
 ('helper_return_reconverges','f(li); workgroupBarrier();',False,'fn f(x:u32){if(x==0u){return;}}\n'),
 ('unused_helper_invalid','',True,'var<workgroup> x:u32; fn bad(){if(x==0u){workgroupBarrier();}}\n'),
 ('mutable_helper_result','if(f()==0u){workgroupBarrier();}',True,RES+'fn f()->u32{return rw[0];}\n'),
 ('workgroup_helper_result','if(f()==0u){workgroupBarrier();}',True,'var<workgroup> x:u32; fn f()->u32{return x;}\n'),
 ('source_barrier_no_roots','workgroupBarrier();',False,''),
 ('builtin_shadow_function','if(li==0u){workgroupBarrier();}',False,'fn workgroupBarrier(){}\n'),
]
for name,body,varying,prefix in helper_cases:
    add(name,body,4 if varying else 0,prefix,path=['callsite_or_formal','callee_requirement_or_return','varying_origin'] if varying else None,
        obligation='Acyclic function summary graph and retained source AST, including runtime-skipped calls')

add('struct_partial_retains','var a=S(li,0u); a.x=0u; if(a.y==0u){workgroupBarrier();}',4,'struct S{x:u32,y:u32};',path=['barrier_cf','read_a','partial_a','old_a','li'])
add('struct_whole_kills','var a=S(li,0u); a=S(0u,0u); if(a.y==0u){workgroupBarrier();}',0,'struct S{x:u32,y:u32};')
add('after_barrier_load_still_varying','workgroupBarrier(); if(x==0u){workgroupBarrier();}',4,'var<workgroup> x:u32;',path=['second_barrier_cf','load_x'])

# Grammar and declaration identity boundaries, statuses are exact ABI values.
for name,prefix,body,status in [
 ('root_bool','var<workgroup> x:bool;','let y=x;',0),
 ('root_i32','var<workgroup> x:i32;','let y=x;',0),
 ('root_f32','var<workgroup> x:f32;','let y=x;',0),
 ('root_vector','var<workgroup> x:vec3u;','let y=x;',0),
 ('root_forward','var<workgroup> x:S; struct S{a:array<vec2u,N>, b:bool}; const N=3u;','let y=x;',0),
 ('root_unused','var<workgroup> x:array<u32,17>;','',0),
 ('root_shadow','var<workgroup> x:u32;','let x=7u; let y=x;',0),
 ('root_initializer','var<workgroup> x:u32=1u;','',4),
 ('root_access_mode','var<workgroup,read_write> x:u32;','',4),
 ('root_runtime_array','var<workgroup> x:array<u32>;','',4),
 ('root_runtime_tail','struct S{a:array<u32>}; var<workgroup> x:S;','',4),
 ('root_zero_array','var<workgroup> x:array<u32,0>;','',4),
 ('root_duplicate','var<workgroup> x:u32; var<workgroup> x:u32;','',4),
 ('root_type_cycle','struct S{a:S}; var<workgroup> x:S;','',4),
 ('barrier_arity','','workgroupBarrier(1u);',4),
 ('barrier_value','','let x=workgroupBarrier();',4),
 ('barrier_const','','const x=workgroupBarrier();',4),
 ('dead_suffix_still_typed','','return; missing();',4),
 ('excluded_atomic','var<workgroup> x:atomic<u32>;','',2),
 ('excluded_storage_barrier','','storageBarrier();',2),
 ('excluded_uniform_load','var<workgroup> x:u32;','workgroupUniformLoad(&x);',2),
]:
    add(name,body,status,prefix,obligation='Exact type/namespace/status boundary; not uniformity fallback')

OUT = '@group(0) @binding(0) var<storage,read_write> out:array<u32>;\n'
def execution(inputs, outputs, groups=(3,1,1), dispatches=2):
    return {'groups':list(groups),'dispatches':dispatches,'inputs_hex':inputs,
            'expected_outputs_hex':outputs,'class':'exact bytes',
            'schedules':['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7'],
            'initial_shared_poison':['a5','5a'],'race_free_required':True}
def u32(v): return struct.pack('<'+'I'*len(v),*[x & 0xffffffff for x in v]).hex()

add('execute_group_isolation','shared[li]=wid.x*100u+li; workgroupBarrier(); out[gid.x]=shared[(li+1u)%4u]; workgroupBarrier();',prefix=OUT+'var<workgroup> shared:array<u32,4>;',expected=execution({'0:0':u32([99]*12)},{'0:0':u32([g*100+(i+1)%4 for g in range(3) for i in range(4)])}),obligation='Cross-lane exchange, distinct groups, generated and two source barriers')
add('execute_zero_before_return','if(li==0u){return;} out[gid.x]=shared[li];',prefix=OUT+'var<workgroup> shared:array<u32,4>;',expected=execution({'0:0':u32([99]*12)},{'0:0':u32([99 if i%4==0 else 0 for i in range(12)])}),obligation='Compiler initialization before source early return; all zero prologue lanes')
add('execute_nested_zero','out[gid.x]=a[li].v.x+a[li].v.y+u32(a[li].b)+u32(a[li].f);',prefix=OUT+'struct S{v:vec2u,b:bool,f:f32}; var<workgroup> a:array<S,4>;',expected=execution({'0:0':u32([99]*12)},{'0:0':u32([0]*12)}),obligation='Whole-root null handles nested bool/vector/f32 aggregate safely')
add('execute_hidden_index','out[0]=x;',prefix=OUT+'var<workgroup> x:u32;',source=OUT+'var<workgroup> x:u32; @compute @workgroup_size(1) fn main(){out[0]=x;}\n',expected=execution({'0:0':u32([99])},{'0:0':u32([0])},(1,1,1)),obligation='Hidden target index for entry with no source builtin, one-lane prologue retained')
add('execute_helper_phases','for(var i=0u;i<3u;i=i+1u){shared[li]=wid.x*100u+i*10u+li; sync(); out[gid.x]=shared[(li+1u)%4u]; sync();}',prefix=OUT+'var<workgroup> shared:array<u32,4>; fn sync(){workgroupBarrier();}',expected=execution({'0:0':u32([99]*12)},{'0:0':u32([g*100+20+(i+1)%4 for g in range(3) for i in range(4)])}),obligation='Same helper barrier at different callsites and loop phases')
add('execute_bounds','shared[li]=li+3u; workgroupBarrier(); shared[99u]=91u; out[gid.x]=shared[li]+shared[99u];',prefix=OUT+'var<workgroup> shared:array<u32,4>;',expected=execution({'0:0':u32([99]*10)},{'0:0':u32([i%4+3 for i in range(10)])}),obligation='Independent shared and storage bounds with partial output length')
add('execute_snapshot','shared[li]=li+3u; workgroupBarrier(); let v=shared[li]; workgroupBarrier(); shared[li]=9u; workgroupBarrier(); out[gid.x]=v;',prefix=OUT+'var<workgroup> shared:array<u32,4>;',expected=execution({'0:0':u32([99]*12)},{'0:0':u32([i%4+3 for i in range(12)])}),obligation='Value snapshots survive later shared writes')
add('control_vector_race','if(li==0u){shared.x=1u;} if(li==1u){shared.y=2u;} workgroupBarrier();',prefix='var<workgroup> shared:vec2u;',expected={'groups':[1,1,1],'oracle_expected_failure':'same-vector conflicting writes','positive_numerical_witness':False},obligation='Distinct vector components can share a memory location; must detect race')
add('control_storage_not_fenced','out[gid.x]=li; workgroupBarrier(); let x=out[wid.x*4u+(li+1u)%4u];',prefix=OUT,expected={'groups':[2,1,1],'inputs_hex':{'0:0':u32([99]*8)},'oracle_expected_failure':'storage race not ordered by WorkgroupMemory','positive_numerical_witness':False},obligation='Workgroup barrier is not a storage fence')

# Unchanged witness bytes and independently evaluated exact source-order arithmetic.
def f32s(v): return struct.pack('<'+'f'*len(v),*v).hex()
def f32(x): return struct.unpack('<f',struct.pack('<f',x))[0]
def witness(name,path,inputs,outputs,groups,scenario):
    src=(REPO/path).read_text()
    add(name,'',source=src,expected=execution(inputs,outputs,(groups,1,1)),obligation=scenario)
    CASES[-1]['original_source_path']=path
witness('witness_zero','bench/kernels/zero_initialize_workgroup_memory_2048.wgsl',{'0:0':f32s([31.0]*2048)},{'0:0':f32s([0.0]*2048)},1,'ONE group: all 2048 +zero outputs, 256 lanes, compact root and repeated poisoned dispatch')
for repeat,path in [(1,'bench/kernels/workgroup_non_atomic.wgsl'),(100,'bench/kernels/workgroup_non_atomic_1024x100.wgsl')]:
    n=507
    vals=[(i*13+7)%251 for i in range(n)]
    expected=[]
    for g in range(2):
        acc=[vals[g*256+i] if g*256+i<n else 0 for i in range(256)]
        for iteration in range(repeat):
            shared=[(x+g*256+i+(iteration if repeat>1 else 0))&0xffffffff for i,x in enumerate(acc)]
            for i in range(256): acc=[shared[(i+x)&255] for x in acc]
        expected+=acc
    witness('witness_nonatomic_'+str(repeat),path,{'0:0':u32(vals)},{'0:0':u32(expected[:n])},2,'256 lanes; partial output507; exact integer indexed cross-lane source order and '+str(2*repeat)+' source barriers')
for length,lhs_n,rhs_n,out_n in [(0,0,0,1),(5,5,3,2),(8,8,8,1),(17,6,7,1)]:
    lhs=[float(i-3) for i in range(lhs_n)]; rhs=[float(2-i) for i in range(rhs_n)]
    part=[f32((lhs[i] if i<lhs_n else 0)*(rhs[i] if i<rhs_n else 0)) if i<length else 0.0 for i in range(8)]
    for stride in (1,2,4):
        for i in range(0,8,2*stride):part[i]=f32(part[i]+part[i+stride])
    witness('witness_pairwise_'+str(length),'bench/inference-pipeline/kernels/dot_product_pairwise_f32.wgsl',{'0:0':u32([length,0,0,0]),'0:1':f32s(lhs),'0:2':f32s(rhs),'0:3':f32s([31.0]*out_n)},{'0:3':f32s([part[0]]+[31.0]*(out_n-1))},1,'ONE group, unequal inputs and partial lengths; pairwise stride1,2,4 exact representable f32')
for cols,hn,wn,on in [(67,65,132,3),(5,5,10,1)]:
    rows=2; hidden=[float(i%5-2) for i in range(hn)]; weights=[float(i%7-3) for i in range(wn)]; result=[]
    for row in range(rows):
        partial=[0.0]*64
        for lane in range(64):
            for col in range(lane,cols,64):partial[lane]=f32(partial[lane]+f32((hidden[col] if col<hn else 0)*(weights[row*cols+col] if row*cols+col<wn else 0)))
        for stride in (32,16,8,4,2,1):
            for lane in range(stride):partial[lane]=f32(partial[lane]+partial[lane+stride])
        result.append(partial[0])
    witness('witness_matmul_'+str(cols),'bench/inference-pipeline/kernels/matmul_logits_tree64_f32.wgsl',{'0:0':u32([rows,cols,0,0]),'0:1':f32s(hidden),'0:2':f32s(weights),'0:3':f32s([31.0]*on)},{'0:3':f32s((result+[31.0])[:on])},3,'64 lanes, cols unequal64, out-of-range group early return, unequal inputs/output and divergent barrier-free accumulation')
for count,sn,dn in [(73,70,3),(128,128,2)]:
    src=[[float((i+j)%9-4) for j in range(4)] for i in range(sn)]; result=[]
    for g in range(3):
        part=[src[g*64+i][:] if g*64+i<min(count,sn) else [0.0]*4 for i in range(64)]
        for stride in (32,16,8,4,2,1):
            for i in range(stride):part[i]=[f32(x+y) for x,y in zip(part[i],part[i+stride])]
        result+=part[0]
    witness('witness_vec4_'+str(count),'bench/kernels/monte_carlo_reduce_vec4.wgsl',{'0:0':u32([count,0,0,0]),'0:1':f32s([x for row in src for x in row]),'0:2':f32s([31.0]*(4*dn))},{'0:2':f32s(result[:4*dn])},3,'64 lanes, whole-vector lane ownership, partial final group and unequal buffer lengths')

# Work controls have exact source bytes; instrumented count N is measured, never guessed.
for count in (16,64,128):
    formal=','.join('p'+str(i)+':u32' for i in range(count))
    args=','.join('0u' for _ in range(count))
    source=f'fn h({formal}){{workgroupBarrier();}} @compute @workgroup_size(1) fn main(){{h({args});}}\n'
    add('work_formals_'+str(count),'',source=source,obligation='Record actual dependency operations; exact N succeeds; N-1 UNSUPPORTED work-limit with atomic output; all formals participate')
for depth in (2,4,8):
    body='workgroupBarrier();'
    for i in range(depth):body=f'for(var i{i}=0u;i{i}<2u;i{i}=i{i}+1u){{{body}}}'
    add('work_nested_'+str(depth),body,obligation='Count work and measure compilation time; fixed depth cases use job limits where needed, no arbitrary iteration cutoff')

# Graph self-check is independent reachability, not the proposed sensitivity solver.
def reachable(g):
    edges={}
    for a,b in g['edges']:edges.setdefault(a,[]).append(b)
    todo=[g['required']]; seen=set()
    while todo:
        node=todo.pop()
        if node==g['nonuniform']:return True
        if node in seen:continue
        seen.add(node);todo.extend(edges.get(node,[]))
    return False
for case in CASES:
    if 'uniformity_graph' in case:
        assert reachable(case['uniformity_graph'])==case['graph_reaches_nonuniform'],case['id']
save_json(FIX/'cases.json',CASES)

controls = [
 {'id':'remove_zero_store','mutation':'Remove whole-root prologue OpStore, retaining declaration/barrier','validator':'valid','must_fail':'poisoned zero output or structural init check'},
 {'id':'remove_prologue_barrier','mutation':'Remove compiler barrier','validator':'valid','must_fail':'workgroup initialization read/write happens-before race'},
 {'id':'remove_source_barrier','mutation':'Remove first source barrier in cross-lane exchange','validator':'valid','must_fail':'shared write/read race independent of lucky numeric scheduling'},
 {'id':'skip_lane_barrier','mutation':'Executor control forces lane1 to skip a barrier event','validator':'not a binary mutation','must_fail':'dynamic rendezvous mismatch/deadlock, never silently release'},
 {'id':'privatize_shared','mutation':'Executor memory ownership split per invocation while target remains Workgroup','validator':'original valid','must_fail':'cross-lane exchange numerical bytes'},
 {'id':'alias_groups','mutation':'Executor maps all workgroups to one shared root identity','validator':'original valid','must_fail':'group-isolation race or differing exact outputs'},
 {'id':'wrong_memory_scope','mutation':'Replace barrier memory scope2 with1','validator':'classify actual result, do not assume','must_fail':'exact barrier contract mismatch'},
 {'id':'wrong_execution_scope','mutation':'Replace barrier execution scope2 with1','validator':'invalid under Vulkan1.1 expected','must_fail':'validator or exact barrier contract mismatch'},
 {'id':'no_memory_semantics','mutation':'Replace0x108 with0','validator':'valid expected','must_fail':'no Workgroup visibility edge, race'},
 {'id':'storage_semantics','mutation':'Replace0x108 with0x48','validator':'classify actual result','must_fail':'wrong semantics structural check, cannot bless StorageBuffer fence'},
 {'id':'helper_phase_mismatch','mutation':'Executor shifts one invocation caller-chain callsite at a barrier','validator':'original valid','must_fail':'dynamic rendezvous call-chain mismatch'},
 {'id':'loop_phase_mismatch','mutation':'Executor shifts one invocation active-loop iteration at same static barrier','validator':'original valid','must_fail':'dynamic rendezvous phase mismatch'},
 {'id':'vector_components','fixture':'control_vector_race','validator':'valid expected','must_fail':'same-vector conflicting memory locations'},
 {'id':'storage_barrier_scope','fixture':'control_storage_not_fenced','validator':'valid expected','must_fail':'storage write/read conflict remains unordered'},
 {'id':'graph_drop_rhs','mutation':'Drop constant-skipped RHS path from explicit graph','must_fail':'expected graph rejection flips for uniform_skipped_and_varying/helper_skipped_and_varying_cf'},
 {'id':'graph_retain_killed','mutation':'Add old varying reaching-definition edge after whole assignment','must_fail':'flow_whole_assignment_kills graph acceptance flips'},
 {'id':'graph_drop_backedge','mutation':'Remove varying backedge edge','must_fail':'flow_loop_break_backedge rejection flips'},
]
save_json(FIX/'controls.json',controls)
plan = {
 'baseline_commit':'2220c64cff0bd87b1eed6b246515941ce7d18d24',
 'contract_manifest_sha256':'9f5013276a311c610cade91062426b9ab6d7f993293c7fcd22ff183902c81f75',
 'normative_reference':'https://www.w3.org/TR/2026/CRD-WGSL-20261021/',
 'candidate_state':{'inspected':False,'built':False,'executed':False,'loaded':False},
 'graph_method':'Explicit dependency edges frozen per source case; independent BFS required-to-varying reachability. These graph slices record the decisive path or its absence, not an alternate WGSL parser or copy of Boolean solver.',
 'target_execution':'Real emitted SPIR-V only. Per-invocation value maps/Function memory and call frames; per-workgroup Workgroup roots; per-dispatch lifetime. Instruction interleaving fair schedules. Dynamic barrier keys include group, static instruction, active caller sites and active loop iteration stack. All live required invocations must rendezvous; termination while peers await is failure. No invocation-at-a-time replay.',
 'memory_model':'Shared storage initially poison; whole typed null store supplies zeros. Independent access epochs or vector-clock happens-before. Workgroup barrier orders only Workgroup memory, not StorageBuffer. Memory conflict granularity includes enclosing vector memory location, arrays/struct fields track true subobject ranges. All racy controls excluded from positive numerical claims.',
 'schedules':{'round_robin':'ascending global invocation IDs, one instruction each','reverse':'descending IDs, one instruction each','shuffle':'Fisher-Yates fair sweep with Python Random seed0x11c0ffee','bounded_burst':'ascending rotation, burst pattern1,3,7 instructions; fair sweeps','dispatches':2,'poison_bytes':[165,90]},
 'native_gates':['Fresh GCC14 C11 strict flags source library/archive/CLI; separately bind handed prebuilt','C/C++ source/archive/shared/prebuilt public consumption','four headers byte-identical and seven exports','current query exact spans and one-short rejection; stale M10 bytes reject','disjoint/overlapping spans; unaligned and boundary sentinels','reentry/concurrent callers; no shared mutable state or heap','fenv/errno restored success/failure every output mode','WORDS/REFLECTION/MEASURE full private emission/uniformity and failure atomicity','ASan/UBSan source tests and new roots/shortcircuits/loop cases','actual records/slices/phase union and target counts versus frozen ledger','complete .su acyclic/recursive combined bounds, fixed212992/job196608','actual counted analysis work every witness; private exact N and N-1 with source-bound instrumentation'],
 'capacity_plan':{'fixed_query_bytes':146256,'cli_query_bytes':2536128,'old_job_cli_profile_query_bytes':2172992,'fixed_workspace_ceiling':147456,'abi1_wrapper_ceiling':147968,'other_frames_ceiling':8192,'acyclic_path_ceiling':16384,'work_ceiling':67108864,'source_caps':'All public M10 capacities unchanged; bind original fixtures and exact current-query mapping','target_ids':'256+D+64I+3F+P+(S+1+X)+10X+R+G+8','target_words':'2048+256I+32F+4P+O+9D+6M+L+4K+57X+40R+25G+64'},
 'historical':'Bind every portable accepted M10 input and original adapter. Preserve original failures. Exact workgroup-negative admission and current private-query updates only; report each mapping before changing a runner. No broad status set, skip, or blanket author-test acceptance.',
 'corpus':'Rebind unchanged same98 source inventory from accepted portable runner; classify emitted, validated, executed and rejected separately. Six mandatory source paths frozen above. Other newly admitted sources require their own independent oracle or remain emitted/validated only.',
 'acceptance':'Every frozen obligation must close. Report precise unqualified gates, not arbitrary-count success. No GPU, runtime metadata adoption, performance, whole-Doe or production claims.',
 'mutability':'This frozen directory and seal remain immutable. Later harness implementations/defect fixes go outside it with attributed supplement and preserved original failures.'}
save_json(FIX/'plan.json',plan)
# Exact inherited file identities, not copies of binaries/caches.
legacy=REPO/'runtime/c/compiler/independent_tests_m10'
parents={str(p.relative_to(REPO)):digest(p.read_bytes()) for p in legacy.rglob('*') if p.is_file() and '__pycache__' not in str(p)}
save_json(FIX/'inherited_inputs.json',parents)
manifest=''.join(digest(p.read_bytes())+'  '+str(p.relative_to(HERE))+'\n' for p in sorted(FIX.rglob('*')) if p.is_file())
(HERE/'CASE_SEAL.sha256').write_text(manifest)
receipt={'utc':datetime.now(timezone.utc).isoformat(),'seal_sha256':digest(manifest.encode()),'case_count':len(CASES),'fixture_files':len(list(FIX.glob('*.wgsl'))),'execution_cases':sum('execution'in x for x in CASES),'graph_cases':sum('uniformity_graph'in x for x in CASES),'controls':len(controls),'contract_verified':True,'candidate_inspected':False,'candidate_executed':False,'author_may_start':True,'unrun':'Candidate compilation, validation and all execution/native/ledger gates remain pending; oracle harness implementation follows seal.'}
save_json(HERE/'CASE_SEAL_RECEIPT.json',receipt)
print(json.dumps(receipt,indent=2))
