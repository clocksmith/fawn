@group(0) @binding(1) var<storage,read_write> output:array<u32>;const result=dot(vec2f(0x1p-149f,0.0f),vec2f(1.0f,1.0f));@compute @workgroup_size(1) fn main(){output[0u]=bitcast<u32>(f32(result));}
