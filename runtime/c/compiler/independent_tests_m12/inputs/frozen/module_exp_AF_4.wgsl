@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=exp(vec4(-2.0,0.0,1.0,3.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
output[3u]=bitcast<u32>(f32(result.w));
}
