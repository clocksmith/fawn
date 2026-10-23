@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a=vec4<u32>(65536u,65536u,65536u,65536u);let b=vec4<u32>(65536u,65536u,65536u,65536u);let result=dot(a,b);output[0u]=result;}
