@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=dot(vec4<f32>(3.0f,-5.0f,7.0f,-11.0f),vec4<f32>(13.0f,17.0f,-19.0f,23.0f));
output[0u]=bitcast<u32>(f32(result));
}
