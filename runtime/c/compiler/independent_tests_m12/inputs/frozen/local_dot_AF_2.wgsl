@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=dot(vec2(3.0,-5.0),vec2(13.0,17.0));
output[0u]=bitcast<u32>(f32(result));
}
