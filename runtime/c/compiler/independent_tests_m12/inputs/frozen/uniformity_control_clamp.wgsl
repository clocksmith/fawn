@compute @workgroup_size(4) fn main(){if(clamp(vec3<f32>(-7.0f,13.0f,-29.0f),vec3<f32>(-9.0f,-4.0f,-31.0f),vec3<f32>(3.0f,17.0f,-2.0f)).x==0.0f){workgroupBarrier();}}
