@group(0) @binding(1) var<storage,read_write> output:array<u32>;
fn helper()->vec3f{return exp(vec3<f32>(-2.0f,0.0f,1.0f));}@compute @workgroup_size(1) fn main(){let result=helper();output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
