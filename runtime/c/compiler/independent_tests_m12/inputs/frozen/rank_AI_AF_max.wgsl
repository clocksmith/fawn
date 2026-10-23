@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=max(vec3(1,2,3),vec3(4.0,5.0,6.0));@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
