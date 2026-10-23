@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@group(0) @binding(0) var<storage,read_write> state:array<u32>;fn effect()->vec2f{state[0]=state[0]+1u;return vec2f(2.0f,3.0f);}@compute @workgroup_size(1) fn main(){let result=false && (dot(effect(),effect())>0.0f);output[0]=u32(result);}
