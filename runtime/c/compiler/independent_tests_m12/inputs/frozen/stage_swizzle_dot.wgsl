@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let result=dot(vec3<f32>(7.0f,-5.0f,3.0f).zyx,vec3<f32>(-19.0f,17.0f,13.0f).zyx);output[0u]=bitcast<u32>(f32(result));}
