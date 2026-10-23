@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=length(vec3(3.0,4.0,-12.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result));
}
