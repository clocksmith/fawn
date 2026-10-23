@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=clamp(vec2<u32>(4294967295u,13u),vec2<u32>(5u,0u),vec2<u32>(99u,17u));
@compute @workgroup_size(1) fn main(){
output[0u]=result.x;
output[1u]=result.y;
}
