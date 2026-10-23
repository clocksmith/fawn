@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){const a=vec2<u32>(4294967295u,4294967295u);const b=vec2<u32>(4294967295u,4294967295u);const result=dot(a,b);output[0u]=result;}
