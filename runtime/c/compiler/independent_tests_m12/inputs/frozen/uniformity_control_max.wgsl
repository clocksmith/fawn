@compute @workgroup_size(4) fn main(){if(max(vec3<f32>(-7.0f,13.0f,-29.0f),vec3<f32>(5.0f,-11.0f,31.0f)).x==0.0f){workgroupBarrier();}}
