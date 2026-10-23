@group(0) @binding(0) var<storage,read_write> out:array<u32>;
@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
out[gid.x]=li; workgroupBarrier(); let x=out[wid.x*4u+(li+1u)%4u];
}
