@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a=vec3<i32>(65536i,65536i,65536i);let b=vec3<i32>(65536i,65536i,65536i);let result=dot(a,b);output[0u]=bitcast<u32>(i32(result));}
