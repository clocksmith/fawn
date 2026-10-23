@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=max(vec3<i32>(-7i,13i,-29i),vec3<i32>(5i,-11i,31i));
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
output[2u]=bitcast<u32>(i32(result.z));
}
