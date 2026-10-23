@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=clamp(vec4(-7,13,-29,41),vec4(-9,-4,-31,0),vec4(3,17,-2,39));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
output[2u]=bitcast<u32>(i32(result.z));
output[3u]=bitcast<u32>(i32(result.w));
}
