@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read> input:array<u32>;
@compute @workgroup_size(1) fn main(){
let result=clamp(input[0u],input[1u],input[2u]);
output[0u]=result;
}
