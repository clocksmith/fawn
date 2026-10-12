struct Input { value: vec4<f32>, exponent: vec4<i32> }
@group(0) @binding(0) var<storage, read> input: Input;
@group(0) @binding(1) var<storage, read_write> output: array<vec4<f32>>;
var<private> value_calls: u32;
var<private> exponent_calls: u32;
fn next_value() -> f32 { value_calls += 1u; return input.value.w; }
fn next_exponent() -> i32 { exponent_calls += 1u; return input.exponent.w; }
@compute @workgroup_size(1) fn main() {
    let x = input.value;
    let e = input.exponent;
    output[0] = vec4<f32>(ldexp(x.x, e.x), ldexp(x.y, e.y), ldexp(x.z, e.z), ldexp(x.w, e.w));
    output[1] = vec4<f32>(ldexp(x.xy, e.xy), 0.0, 0.0);
    output[2] = vec4<f32>(ldexp(x.xyz, e.xyz), 0.0);
    output[3] = ldexp(x, e);
    let side_effect = ldexp(next_value(), next_exponent());
    output[4] = vec4<f32>(side_effect, f32(value_calls), f32(exponent_calls), 0.0);
    output[5] = saturate(x);
    output[6] = degrees(x);
    output[7] = radians(x);
}
