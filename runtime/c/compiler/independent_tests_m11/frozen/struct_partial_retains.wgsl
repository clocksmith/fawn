struct S{x:u32,y:u32};@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
var a=S(li,0u); a.x=0u; if(a.y==0u){workgroupBarrier();}
}
