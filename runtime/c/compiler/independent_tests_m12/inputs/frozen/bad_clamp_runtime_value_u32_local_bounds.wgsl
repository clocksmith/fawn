@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let runtime_value=vec3<u32>(2u,3u,4u);const lower=vec3<u32>(1u,9u,1u);const upper=vec3<u32>(5u,5u,5u);let result=clamp(runtime_value,lower,upper);}
