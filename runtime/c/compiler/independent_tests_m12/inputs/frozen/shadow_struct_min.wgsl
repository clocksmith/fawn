@group(0) @binding(1) var<storage,read_write> output:array<u32>;
struct min{x:u32,y:u32};@compute @workgroup_size(1) fn main(){let result=min(9u,4u);output[0]=result.x;output[1]=result.y;}
