@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec2<i32>(3i,-5i),vec2<i32>(13i,17i));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result));
}
