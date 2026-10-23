@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32){if(false && (cross(vec3<f32>(1.0f,2.0f,3.0f),vec3f(f32(li),0.0f,0.0f)).x==0.0f)){workgroupBarrier();}}
