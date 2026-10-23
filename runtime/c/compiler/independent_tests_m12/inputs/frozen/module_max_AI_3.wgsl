@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=max(vec3(-7,13,-29),vec3(5,-11,31));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result.x));
output[1u]=bitcast<u32>(i32(result.y));
output[2u]=bitcast<u32>(i32(result.z));
}
