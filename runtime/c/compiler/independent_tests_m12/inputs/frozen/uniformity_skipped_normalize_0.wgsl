@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32){if(false && (normalize(vec3f(f32(li),0.0f,0.0f)).x==0.0f)){workgroupBarrier();}}
