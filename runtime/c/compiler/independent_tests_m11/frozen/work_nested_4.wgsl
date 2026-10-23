@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
for(var i3=0u;i3<2u;i3=i3+1u){for(var i2=0u;i2<2u;i2=i2+1u){for(var i1=0u;i1<2u;i1=i1+1u){for(var i0=0u;i0<2u;i0=i0+1u){workgroupBarrier();}}}}
}
