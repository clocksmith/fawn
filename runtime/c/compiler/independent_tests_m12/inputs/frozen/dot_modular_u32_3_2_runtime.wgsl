@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a=vec3<u32>(4294967295u,4294967295u,4294967295u);let b=vec3<u32>(4294967295u,4294967295u,4294967295u);let result=dot(a,b);output[0u]=result;}
