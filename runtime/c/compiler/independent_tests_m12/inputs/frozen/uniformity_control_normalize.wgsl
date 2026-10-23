@compute @workgroup_size(4) fn main(){if(normalize(vec3<f32>(3.0f,4.0f,-12.0f)).x==0.0f){workgroupBarrier();}}
