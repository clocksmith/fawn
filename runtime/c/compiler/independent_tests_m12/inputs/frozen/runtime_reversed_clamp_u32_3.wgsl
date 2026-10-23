@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=vec3<u32>(2u,8u,17u);let a1=vec3<u32>(9u,4u,23u);let a2=vec3<u32>(3u,6u,11u);let result=clamp(a0,a1,a2);output[0u]=result.x;
output[1u]=result.y;
output[2u]=result.z;}
