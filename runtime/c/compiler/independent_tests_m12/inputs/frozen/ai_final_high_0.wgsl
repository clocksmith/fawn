@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec2(9223372036854775807,1),vec2(1,1));
@compute @workgroup_size(1) fn main(){}
