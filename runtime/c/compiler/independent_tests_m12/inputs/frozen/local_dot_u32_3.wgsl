@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=dot(vec3<u32>(4294967295u,65536u,2147483648u),vec3<u32>(4294967295u,65536u,3u));
output[0u]=result;
}
