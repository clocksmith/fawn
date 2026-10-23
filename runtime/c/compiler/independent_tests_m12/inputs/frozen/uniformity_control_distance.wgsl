@compute @workgroup_size(4) fn main(){if(distance(vec3<f32>(-1.0f,2.0f,7.0f),vec3<f32>(2.0f,6.0f,-5.0f))==0.0f){workgroupBarrier();}}
