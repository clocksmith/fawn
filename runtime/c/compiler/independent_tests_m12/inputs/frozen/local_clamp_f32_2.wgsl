@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=clamp(vec2<f32>(-7.0f,13.0f),vec2<f32>(-9.0f,-4.0f),vec2<f32>(3.0f,17.0f));
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
}
