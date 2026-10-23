@compute @workgroup_size(4) fn main(){if(sqrt(vec3<f32>(1.0f,4.0f,9.0f)).x==0.0f){workgroupBarrier();}}
