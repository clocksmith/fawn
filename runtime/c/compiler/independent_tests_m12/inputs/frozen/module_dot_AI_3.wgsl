@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec3(3,-5,7),vec3(13,17,-19));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(i32(result));
}
