@compute @workgroup_size(4) fn main(){if(length(vec3<f32>(3.0f,4.0f,-12.0f))==0.0f){workgroupBarrier();}}
