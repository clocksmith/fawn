@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=clamp(vec3<u32>(4294967295u,13u,2147483648u),vec3<u32>(5u,0u,2147483647u),vec3<u32>(99u,17u,2147483649u));
output[0u]=result.x;
output[1u]=result.y;
output[2u]=result.z;
}
