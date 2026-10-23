@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=clamp(vec2<i32>(-7i,13i),vec2<i32>(-9i,-4i),vec2<i32>(3i,17i));
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
}
