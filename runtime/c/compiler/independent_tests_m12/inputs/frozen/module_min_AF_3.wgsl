@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=min(vec3(-7.0,13.0,-29.0),vec3(5.0,-11.0,31.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
}
