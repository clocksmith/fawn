@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=array<vec3f,1>(vec3<f32>(3.0f,-5.0f,7.0f));let a1=array<vec3f,1>(vec3<f32>(13.0f,17.0f,-19.0f));let result=dot(a0[0],a1[0]);output[0u]=bitcast<u32>(f32(result));}
