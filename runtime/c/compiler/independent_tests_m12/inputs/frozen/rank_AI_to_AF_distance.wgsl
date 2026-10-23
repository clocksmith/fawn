@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=distance(vec3(-1,2,7),vec3(2,6,-5));@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(f32(result));}
