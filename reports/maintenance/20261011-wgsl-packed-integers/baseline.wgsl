@group(0) @binding(0) var<storage, read> input: array<f32>;
@group(0) @binding(1) var<storage, read_write> output: array<u32>;
@compute @workgroup_size(1) fn main() {
  output[0] = pack4x8unorm(vec4f(input[0], input[1], input[2], input[3]));
  output[1] = pack4x8snorm(vec4f(input[0], input[1], input[2], input[3]));
  output[2] = pack2x16float(vec2f(input[0], input[1]));
}
