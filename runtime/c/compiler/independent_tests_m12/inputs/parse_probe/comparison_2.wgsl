@group(0) @binding(1) var<storage,read_write> output:array<u32>;@compute @workgroup_size(1) fn main(){let a=2u;let b=4u;let c=3u;output[0]=u32(b>a);}
