@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){const a=vec3<i32>(2147483647i,2147483647i,2147483647i);const b=vec3<i32>(1i,1i,1i);const result=dot(a,b);output[0u]=bitcast<u32>(i32(result));}
