@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=vec2<u32>(2u,8u);let a1=vec2<u32>(9u,4u);let a2=vec2<u32>(3u,6u);let result=clamp(a0,a1,a2);output[0u]=result.x;
output[1u]=result.y;}
