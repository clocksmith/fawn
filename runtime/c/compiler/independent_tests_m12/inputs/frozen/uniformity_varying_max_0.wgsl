@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) li:u32){if(max(vec3f(f32(li),0.0f,0.0f),vec3<f32>(5.0f,-11.0f,31.0f)).x==0.0f){workgroupBarrier();}}
