@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=distance(vec4(-1.0,2.0,7.0,-4.0),vec4(2.0,6.0,-5.0,1.0));
output[0u]=bitcast<u32>(f32(result));
}
