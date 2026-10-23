@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let runtime_value=vec3<i32>(2i,3i,4i);let result=clamp(runtime_value,vec3<i32>(1i,9i,1i),vec3<i32>(5i,5i,5i));}
