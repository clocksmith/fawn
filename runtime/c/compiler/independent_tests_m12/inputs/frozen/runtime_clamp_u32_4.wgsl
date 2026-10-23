@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read> input:array<u32>;
@compute @workgroup_size(1) fn main(){
let result=clamp(vec4<u32>(input[0u],input[1u],input[2u],input[3u]),vec4<u32>(input[4u],input[5u],input[6u],input[7u]),vec4<u32>(input[8u],input[9u],input[10u],input[11u]));
output[0u]=result.x;
output[1u]=result.y;
output[2u]=result.z;
output[3u]=result.w;
}
