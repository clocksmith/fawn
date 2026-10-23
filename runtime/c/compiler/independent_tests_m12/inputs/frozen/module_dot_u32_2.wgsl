@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec2<u32>(4294967295u,65536u),vec2<u32>(4294967295u,65536u));
@compute @workgroup_size(1) fn main(){
output[0u]=result;
}
