@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=clamp(-7,-9,3);
output[0u]=bitcast<u32>(i32(result));
}
