@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=length(3.0f);
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result));
}
