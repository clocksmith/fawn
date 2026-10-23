@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read> input:array<u32>;
@compute @workgroup_size(1) fn main(){
let result=dot(vec4<u32>(input[0u],input[1u],input[2u],input[3u]),vec4<u32>(input[4u],input[5u],input[6u],input[7u]));
output[0u]=result;
}
