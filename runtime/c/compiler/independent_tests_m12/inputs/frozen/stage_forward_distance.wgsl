@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=distance(a0,a1);const a0=vec3<f32>(-1.0f,2.0f,7.0f);const a1=vec3<f32>(2.0f,6.0f,-5.0f);@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(f32(result));}
