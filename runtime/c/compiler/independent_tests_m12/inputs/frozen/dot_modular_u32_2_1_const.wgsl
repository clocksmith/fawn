@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){const a=vec2<u32>(2147483647u,2147483647u);const b=vec2<u32>(1u,1u);const result=dot(a,b);output[0u]=result;}
