@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=array<vec3f,1>(vec3<f32>(-7.0f,13.0f,-29.0f));let a1=array<vec3f,1>(vec3<f32>(-9.0f,-4.0f,-31.0f));let a2=array<vec3f,1>(vec3<f32>(3.0f,17.0f,-2.0f));let result=clamp(a0[0],a1[0],a2[0]);output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
