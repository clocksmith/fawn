struct Params { x:u32, y:u32, z:u32, w:u32 };
@group(0) @binding(0) var<uniform> params:Params;
@group(0) @binding(1) var<storage,read> ro:array<u32>;
@group(0) @binding(2) var<storage,read_write> rw:array<u32>;
fn f()->u32{return rw[0];}
@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
if(f()==0u){workgroupBarrier();}
}
