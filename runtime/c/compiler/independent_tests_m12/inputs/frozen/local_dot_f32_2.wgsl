@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=dot(vec2<f32>(3.0f,-5.0f),vec2<f32>(13.0f,17.0f));
output[0u]=bitcast<u32>(f32(result));
}
