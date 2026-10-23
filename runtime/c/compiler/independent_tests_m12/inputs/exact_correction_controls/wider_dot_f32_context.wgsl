@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec2(2147483648,1),vec2(1,1));@compute @workgroup_size(1) fn main(){let value:f32=result;output[0]=bitcast<u32>(value);}
