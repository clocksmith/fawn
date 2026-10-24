@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var<storage, read_write> out_data: array<f32>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let uv = vec2f(0.5, 0.5);
    out_data[id.x] = textureSample(tex, samp, uv).x;
}