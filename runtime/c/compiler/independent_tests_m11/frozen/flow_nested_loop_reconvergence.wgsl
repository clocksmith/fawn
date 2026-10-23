@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
for(var i=0u;i<2u;i=i+1u){for(var j=li;j<4u;j=j+1u){} workgroupBarrier();}
}
