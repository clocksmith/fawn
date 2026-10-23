@compute @workgroup_size(4) fn main(){if(exp(vec3<f32>(-2.0f,0.0f,1.0f)).x==0.0f){workgroupBarrier();}}
