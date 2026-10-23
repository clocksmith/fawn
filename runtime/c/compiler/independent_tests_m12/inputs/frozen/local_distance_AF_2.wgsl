@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=distance(vec2(-1.0,2.0),vec2(2.0,6.0));
output[0u]=bitcast<u32>(f32(result));
}
