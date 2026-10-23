@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read_write> state:array<u32>;
fn arg0()->vec3f{state[0]=state[0]*10u+1u;state[1u]=state[1u]+1u;return vec3<f32>(3.0f,4.0f,-12.0f);}
@compute @workgroup_size(1) fn main(){let result=normalize(arg0());output[0u]=bitcast<u32>(f32(result.x));
output[1u]=bitcast<u32>(f32(result.y));
output[2u]=bitcast<u32>(f32(result.z));}
