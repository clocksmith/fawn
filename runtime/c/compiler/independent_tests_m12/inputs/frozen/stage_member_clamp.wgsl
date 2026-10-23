@group(0) @binding(1) var<storage,read_write> output:array<u32>;
struct Box{x:vec3f};@compute @workgroup_size(1) fn main(){let a0=Box(vec3<f32>(-7.0f,13.0f,-29.0f));let a1=Box(vec3<f32>(-9.0f,-4.0f,-31.0f));let a2=Box(vec3<f32>(3.0f,17.0f,-2.0f));let result=clamp(a0.x,a1.x,a2.x);output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
