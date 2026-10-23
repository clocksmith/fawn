@compute @workgroup_size(4) fn main(){if(dot(vec3<f32>(3.0f,-5.0f,7.0f),vec3<f32>(13.0f,17.0f,-19.0f))==0.0f){workgroupBarrier();}}
