@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=normalize(vec4(3.0,4.0,-12.0,0.0));
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
output[3u]=bitcast<u32>(f32(result.w));
}
