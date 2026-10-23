@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=exp(vec4<f32>(-2.0f,0.0f,1.0f,3.0f));
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
output[3u]=bitcast<u32>(f32(result.w));
}
