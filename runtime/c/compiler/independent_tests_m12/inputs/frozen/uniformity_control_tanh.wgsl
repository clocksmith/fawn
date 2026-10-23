@compute @workgroup_size(4) fn main(){if(tanh(vec3<f32>(-3.0f,-1.0f,0.0f)).x==0.0f){workgroupBarrier();}}
