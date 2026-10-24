@group(0) @binding(0) var<storage, read_write> out_data: array<u32>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let mask = (id.xyxy < vec4<u32>(2u)) & vec4<bool>(true, false, true, true);
    let value = select(0u, 1u, any(mask.yw)) + select(0u, 2u, all(mask.xw));
    out_data[0] = value;
}