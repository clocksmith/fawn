@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=distance(vec2<f32>(-1.0f,2.0f),vec2<f32>(2.0f,6.0f));
output[0u]=bitcast<u32>(f32(result));
}
