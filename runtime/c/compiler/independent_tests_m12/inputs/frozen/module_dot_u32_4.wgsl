@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec4<u32>(4294967295u,65536u,2147483648u,11u),vec4<u32>(4294967295u,65536u,3u,23u));
@compute @workgroup_size(1) fn main(){
output[0u]=result;
}
