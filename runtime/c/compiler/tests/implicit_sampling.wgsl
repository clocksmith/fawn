// Public fragment fixture, unchanged across compiler targets and runtime calls.
// Render4x4; color/depth textures are8x8 with4 mips. Nearest min/mag/mip.
// Color mip0/1/2/3 = red/green/blue/white. Depth = .25/.75/.5/.125.
// params.x scales framebuffer coordinates; params.y is the comparison reference.
// params.x = .125/.25/.5 selects mip0/1/2 respectively (one/two/four texels per pixel).
// With params.y=.5 and CompareLess, literal RGBA8 is [255,0,0,0],
// [0,255,0,255], [0,0,255,0]. Greater flips the first two alphas;
// Equal yields alpha255 only at mip2. Clamping sampler maxLOD to1 at scale.5
// gives the mip1 result for both operations. No CPU shader evaluation is needed.
@group(0) @binding(0) var color_image:texture_2d<f32>;
@group(0) @binding(1) var regular:sampler;
@group(0) @binding(2) var depth_image:texture_depth_2d;
@group(0) @binding(3) var comparison:sampler_comparison;
@group(0) @binding(4) var<uniform> params:vec4f;

fn sampled(uv:vec2f,reference:f32)->vec4f {
    let color=textureSample(color_image,regular,uv);
    let comparison_result=textureSampleCompare(depth_image,comparison,uv,reference);
    return vec4f(color.rgb,comparison_result);
}
fn indirect(uv:vec2f,reference:f32)->vec4f {
    return sampled(uv,reference);
}
@fragment fn main(@builtin(position) position:vec4f)->@location(0) vec4f {
    return indirect(position.xy*params.x,params.y);
}
