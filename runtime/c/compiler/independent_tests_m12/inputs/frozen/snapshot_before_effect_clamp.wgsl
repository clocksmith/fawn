@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read_write> input:array<f32>;fn mutate()->vec3f{input[0]=91.0f;input[1]=92.0f;input[2]=93.0f;return vec3<f32>(-9.0f,-4.0f,-31.0f);}@compute @workgroup_size(1) fn main(){let result=clamp(vec3f(input[0],input[1],input[2]),mutate(),vec3<f32>(3.0f,17.0f,-2.0f));output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
