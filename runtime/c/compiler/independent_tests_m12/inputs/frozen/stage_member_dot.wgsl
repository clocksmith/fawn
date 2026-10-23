@group(0) @binding(1) var<storage,read_write> output:array<u32>;
struct Box{x:vec3f};@compute @workgroup_size(1) fn main(){let a0=Box(vec3<f32>(3.0f,-5.0f,7.0f));let a1=Box(vec3<f32>(13.0f,17.0f,-19.0f));let result=dot(a0.x,a1.x);output[0u]=bitcast<u32>(f32(result));}
