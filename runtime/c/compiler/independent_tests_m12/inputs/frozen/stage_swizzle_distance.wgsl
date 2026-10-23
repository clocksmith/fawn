@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let result=distance(vec3<f32>(7.0f,2.0f,-1.0f).zyx,vec3<f32>(-5.0f,6.0f,2.0f).zyx);output[0u]=bitcast<u32>(f32(result));}
