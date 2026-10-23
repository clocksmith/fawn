@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec2(4294967296,4294967296),vec2(4294967296,-4294967296));
@compute @workgroup_size(1) fn main(){if(result==0){output[0]=1u;}else{output[0]=2u;}}
