@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){let runtime_value=vec3<f32>(2.0f,3.0f,4.0f);const lower=vec3<f32>(1.0f,9.0f,1.0f);const upper=vec3<f32>(5.0f,5.0f,5.0f);let result=clamp(runtime_value,lower,upper);}
