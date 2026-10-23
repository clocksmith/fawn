@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=vec2<f32>(2.0f,8.0f);let a1=vec2<f32>(9.0f,4.0f);let a2=vec2<f32>(3.0f,6.0f);let result=clamp(a0,a1,a2);output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));}
