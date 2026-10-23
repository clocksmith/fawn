@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let result=max(vec3<f32>(-29.0f,13.0f,-7.0f).zyx,vec3<f32>(31.0f,-11.0f,5.0f).zyx);output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
