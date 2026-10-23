@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=min(vec4<i32>(-7i,13i,-29i,41i),vec4<i32>(5i,-11i,31i,-43i));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
output[2u]=bitcast<u32>(i32(result.z));
output[3u]=bitcast<u32>(i32(result.w));
}
