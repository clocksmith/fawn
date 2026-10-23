@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec3(1,2,3),vec3<i32>(4i,5i,6i));@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(i32(result));}
