@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=clamp(vec2(-7,13),vec2(-9,-4),vec2(3,17));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
}
