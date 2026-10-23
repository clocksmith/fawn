@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read> input:array<i32>;
@compute @workgroup_size(1) fn main(){
let result=min(input[0u],input[1u]);
output[0u]=bitcast<u32>(i32(result));
}
