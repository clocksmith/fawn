@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=normalize(vec3(3.0,4.0,-12.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
}
