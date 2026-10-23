@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=min(vec3(1,2,3),vec3<u32>(4u,5u,6u));@compute @workgroup_size(1) fn main(){output[0u]=result.x;
output[1u]=result.y;
output[2u]=result.z;}
