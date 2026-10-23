@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=min(vec2<f32>(-7.0f,13.0f),vec2<f32>(5.0f,-11.0f));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
}
