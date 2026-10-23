@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec2(3,-5),vec2(13,17));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result));
}
