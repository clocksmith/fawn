@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let a0=array<vec3f,1>(vec3<f32>(3.0f,4.0f,-12.0f));let result=length(a0[0]);output[0u]=bitcast<u32>(f32(result));}
