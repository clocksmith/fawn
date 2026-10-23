@compute @workgroup_size(4) fn main(){if(cross(vec3<f32>(1.0f,2.0f,3.0f),vec3<f32>(4.0f,5.0f,6.0f)).x==0.0f){workgroupBarrier();}}
