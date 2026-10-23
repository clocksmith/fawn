@group(0) @binding(1) var<storage,read_write> output:array<u32>;
const result=dot(vec4(3.0,-5.0,7.0,-11.0),vec4(13.0,17.0,-19.0,23.0));
@compute @workgroup_size(1) fn main(){
output[0u]=bitcast<u32>(f32(result));
}
