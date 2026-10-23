@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=dot(vec2<i32>(3i,-5i),vec2<i32>(13i,17i));
output[0u]=bitcast<u32>(i32(result));
}
