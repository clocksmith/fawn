@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=vec4<f32>(2.0f,8.0f,17.0f,29.0f);let a1=vec4<f32>(9.0f,4.0f,23.0f,7.0f);let a2=vec4<f32>(3.0f,6.0f,11.0f,5.0f);let result=clamp(a0,a1,a2);output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
output[3u]=bitcast<u32>(f32(result.w));}
