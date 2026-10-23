@group(0) @binding(1) var<storage,read_write> output:array<u32>;
fn helper()->f32{return distance(vec3<f32>(-1.0f,2.0f,7.0f),vec3<f32>(2.0f,6.0f,-5.0f));}@compute @workgroup_size(1) fn main(){let result=helper();output[0u]=bitcast<u32>(f32(result));}
