@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=max(vec2<i32>(-7i,13i),vec2<i32>(5i,-11i));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
}
