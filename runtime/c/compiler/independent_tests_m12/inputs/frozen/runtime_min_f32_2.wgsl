@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read> input:array<f32>;
@compute @workgroup_size(1) fn main(){
let result=min(vec2<f32>(input[0u],input[1u]),vec2<f32>(input[2u],input[3u]));
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
}
