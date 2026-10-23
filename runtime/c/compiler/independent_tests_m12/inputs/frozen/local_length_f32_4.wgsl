@group(0) @binding(1) var<storage,read_write> output:array<u32>;
@compute @workgroup_size(1) fn main(){
const result=length(vec4<f32>(3.0f,4.0f,-12.0f,0.0f));
output[0u]=bitcast<u32>(f32(result));
}
