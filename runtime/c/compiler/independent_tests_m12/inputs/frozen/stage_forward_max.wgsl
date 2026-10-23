@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=max(a0,a1);const a0=vec3<f32>(-7.0f,13.0f,-29.0f);const a1=vec3<f32>(5.0f,-11.0f,31.0f);@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
