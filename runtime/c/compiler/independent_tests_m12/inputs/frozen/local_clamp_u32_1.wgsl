@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=clamp(4294967295u,5u,99u);
output[0u]=result;
}
