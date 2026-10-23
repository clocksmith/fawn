@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=max(-7.0f,5.0f);
output[0u]=bitcast<u32>(f32(result));
}
