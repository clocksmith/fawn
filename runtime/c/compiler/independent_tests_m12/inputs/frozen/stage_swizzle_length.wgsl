@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let result=length(vec3<f32>(-12.0f,4.0f,3.0f).zyx);output[0u]=bitcast<u32>(f32(result));}
