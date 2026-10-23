@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=max(vec2(-7.0,13.0),vec2(5.0,-11.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
}
