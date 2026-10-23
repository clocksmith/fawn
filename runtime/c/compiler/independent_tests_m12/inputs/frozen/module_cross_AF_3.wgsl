@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=cross(vec3(1.0,2.0,3.0),vec3(4.0,5.0,6.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));
}
