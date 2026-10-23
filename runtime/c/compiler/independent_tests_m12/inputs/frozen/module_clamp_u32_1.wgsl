@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=clamp(4294967295u,5u,99u);
@compute @workgroup_size(1) fn main(){
output[0u]=result;
}
