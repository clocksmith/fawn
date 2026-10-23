@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec3(-1,1,9223372036854775807),vec3(1,1,1));
@compute @workgroup_size(1) fn main(){if(result==9223372036854775807){output[0]=1u;}else{output[0]=2u;}}
