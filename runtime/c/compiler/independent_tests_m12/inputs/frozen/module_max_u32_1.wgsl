@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=max(4294967295u,5u);
@compute @workgroup_size(1) fn main(){
output[0u]=result;
}
