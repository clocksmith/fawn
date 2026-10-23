@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=clamp(vec4(-7.0,13.0,-29.0,41.0),vec4(-9.0,-4.0,-31.0,0.0),vec4(3.0,17.0,-2.0,39.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
output[3u]=bitcast<u32>(f32(result.w));
}
