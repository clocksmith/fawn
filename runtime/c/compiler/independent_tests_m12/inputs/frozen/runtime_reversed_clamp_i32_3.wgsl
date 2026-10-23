@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=vec3<i32>(2i,8i,17i);let a1=vec3<i32>(9i,4i,23i);let a2=vec3<i32>(3i,6i,11i);let result=clamp(a0,a1,a2);output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
output[2u]=bitcast<u32>(i32(result.z));}
