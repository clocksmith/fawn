@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let result=dot(vec2(2147483648,1),vec2(1,1));output[0]=u32(result);}
