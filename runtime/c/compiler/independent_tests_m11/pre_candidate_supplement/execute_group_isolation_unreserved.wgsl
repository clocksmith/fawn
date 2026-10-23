@group(0) @binding(0) var<storage,read_write> out:array<u32>;
var<workgroup> wg_scratch:array<u32,4>;@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
wg_scratch[li]=wid.x*100u+li; workgroupBarrier(); out[gid.x]=wg_scratch[(li+1u)%4u]; workgroupBarrier();
}
