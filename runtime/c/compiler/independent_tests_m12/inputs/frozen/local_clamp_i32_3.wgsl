@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=clamp(vec3<i32>(-7i,13i,-29i),vec3<i32>(-9i,-4i,-31i),vec3<i32>(3i,17i,-2i));
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
output[2u]=bitcast<u32>(i32(result.z));
}
