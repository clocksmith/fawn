@group(0) @binding(0) var<storage,read_write> out:array<u32>;
var<workgroup> wg_scratch:array<u32,4>; fn sync(){workgroupBarrier();}@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
for(var i=0u;i<3u;i=i+1u){wg_scratch[li]=wid.x*100u+i*10u+li; sync(); out[gid.x]=wg_scratch[(li+1u)%4u]; sync();}
}
