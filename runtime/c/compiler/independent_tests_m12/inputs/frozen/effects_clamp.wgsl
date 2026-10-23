@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read_write> state:array<u32>;
fn arg0()->vec3f{state[0]=state[0]*10u+1u;state[1u]=state[1u]+1u;return vec3<f32>(-7.0f,13.0f,-29.0f);}
fn arg1()->vec3f{state[0]=state[0]*10u+2u;state[2u]=state[2u]+1u;return vec3<f32>(-9.0f,-4.0f,-31.0f);}
fn arg2()->vec3f{state[0]=state[0]*10u+3u;state[3u]=state[3u]+1u;return vec3<f32>(3.0f,17.0f,-2.0f);}
@compute @workgroup_size(1) fn main(){let result=clamp(arg0(),arg1(),arg2());output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
