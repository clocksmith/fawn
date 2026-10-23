@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec3(16777216.0,1.0,-16777216.0),vec3(1.0,1.0,1.0));@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(f32(result));}
