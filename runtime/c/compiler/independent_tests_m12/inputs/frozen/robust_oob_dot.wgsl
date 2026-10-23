@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read> input:array<vec3u>;@compute @workgroup_size(1) fn main(){let result=dot(input[99u],vec3u(7u,11u,13u));output[0]=result;}
