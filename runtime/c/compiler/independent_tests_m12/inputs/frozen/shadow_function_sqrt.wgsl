@group(0) @binding(1) var<storage,read_write> output:array<u32>;
fn sqrt(x:u32)->u32{return x+5u;}@compute @workgroup_size(1) fn main(){output[0]=sqrt(7u);}
