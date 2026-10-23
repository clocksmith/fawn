@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32,@builtin(local_invocation_id) lid:vec3u,@builtin(global_invocation_id) gid:vec3u,@builtin(workgroup_id) wid:vec3u,@builtin(num_workgroups) nw:vec3u){
var i=0u; loop{if(i==2u){break;} var j=0u; loop{workgroupBarrier(); if(j==li){break;} j=j+1u;} i=i+1u;}
}
