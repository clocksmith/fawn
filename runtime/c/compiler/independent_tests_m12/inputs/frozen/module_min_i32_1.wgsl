@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=min(-7i,5i);
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result));
}
