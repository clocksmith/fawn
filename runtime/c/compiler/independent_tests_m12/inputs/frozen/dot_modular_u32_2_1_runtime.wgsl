@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a=vec2<u32>(2147483647u,2147483647u);let b=vec2<u32>(1u,1u);let result=dot(a,b);output[0u]=result;}
