// Depth2D texels at mip0: [0.25,0.75], mip1: [0.5].
// Depth2DArray layer0 is 0.25 and layer1 is 0.75. Both use nearest filtering.
// WebGPU compare direction: supplied depth_ref compared to sampled depth.
@group(0) @binding(0) var depth:texture_depth_2d;
@group(0) @binding(1) var layers:texture_depth_2d_array;
@group(0) @binding(2) var regular:sampler;
@group(0) @binding(3) var comparison:sampler_comparison;
@group(0) @binding(4) var<storage,read_write> output:array<u32>;

fn coordinate()->vec2f {
    output[20] = output[20]*10u+1u;
    return vec2f(0.25,0.5);
}
fn reference()->f32 {
    output[20] = output[20]*10u+2u;
    return 0.5;
}
fn layer()->i32 {
    output[20] = output[20]*10u+3u;
    return -1;
}
fn mip()->u32 {
    output[20] = output[20]*10u+4u;
    return 1u;
}
@compute @workgroup_size(1)
fn main() {
    output[0]=bitcast<u32>(textureSampleLevel(depth,regular,vec2f(0.25,0.5),0i));
    output[1]=bitcast<u32>(textureSampleLevel(depth,regular,vec2f(0.75,0.5),0u));
    output[2]=bitcast<u32>(textureSampleLevel(depth,regular,vec2f(0.25,0.5),1i));
    output[3]=bitcast<u32>(textureSampleCompareLevel(depth,comparison,vec2f(0.25,0.5),0.5));
    output[4]=bitcast<u32>(textureSampleCompareLevel(depth,comparison,vec2f(0.75,0.5),0.5));
    output[5]=bitcast<u32>(textureSampleCompareLevel(layers,comparison,vec2f(0.5),-1i,0.5));
    output[6]=bitcast<u32>(textureSampleCompareLevel(layers,comparison,vec2f(0.5),0xffffffffu,0.5));
    output[7]=bitcast<u32>(textureSampleLevel(layers,regular,vec2f(0.5),0xffffffffu,0i));
    output[8]=bitcast<u32>(textureSampleCompareLevel(depth,comparison,vec2f(0.25,0.5),0.5,vec2i(1,0)));
    output[9]=bitcast<u32>(textureSampleLevel(depth,regular,vec2f(0.25,0.5),0u,vec2i(1,0)));
    output[10]=bitcast<u32>(textureSampleLevel(depth,regular,vec2f(0.75,0.5),0i,vec2i(1,0)));
    output[11]=bitcast<u32>(textureSampleCompareLevel(depth,comparison,vec2f(0.25,0.5),0.25));
    output[20]=0u;
    output[12]=bitcast<u32>(textureSampleCompareLevel(depth,comparison,coordinate(),reference()));
    output[13]=output[20];
    output[20]=0u;
    output[14]=bitcast<u32>(textureSampleLevel(layers,regular,coordinate(),layer(),mip()));
    output[15]=output[20];
    output[0xffffffffu]=99u;
    output[16]=output[0xffffffffu];
}
