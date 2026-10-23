@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32){if(false && (cross(vec3f(f32(li),0.0f,0.0f),vec3<f32>(4.0f,5.0f,6.0f)).x==0.0f)){workgroupBarrier();}}
