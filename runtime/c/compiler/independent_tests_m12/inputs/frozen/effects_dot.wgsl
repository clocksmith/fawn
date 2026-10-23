@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read_write> state:array<u32>;
fn arg0()->vec3f{state[0]=state[0]*10u+1u;state[1u]=state[1u]+1u;return vec3<f32>(3.0f,-5.0f,7.0f);}
fn arg1()->vec3f{state[0]=state[0]*10u+2u;state[2u]=state[2u]+1u;return vec3<f32>(13.0f,17.0f,-19.0f);}
@compute @workgroup_size(1) fn main(){let result=dot(arg0(),arg1());output[0u]=bitcast<u32>(f32(result));}
