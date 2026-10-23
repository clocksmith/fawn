@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec3(1,2,3),vec3<f32>(4.0f,5.0f,6.0f));@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(f32(result));}
