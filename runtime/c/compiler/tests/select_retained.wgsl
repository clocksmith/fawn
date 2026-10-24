@group(0) @binding(0) var<storage, read_write> data: array<f32>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    data[id.x] = select(0.0, 1.0, data[id.x] > 0.5);
}