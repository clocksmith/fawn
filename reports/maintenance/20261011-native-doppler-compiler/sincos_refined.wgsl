@group(0) @binding(0) var<storage, read_write> buf: array<f32>;
fn sincos_refined(angle: f32) -> vec2<f32> {
  // Above this range the Q31 constant error can exceed F32 trig accuracy.
  // Preserve the existing builtin path for those larger arguments.
  if (abs(angle) > 16384.0) { return vec2<f32>(cos(angle), sin(angle)); }
  let quadrant = round(angle * 0.6366197723675814);
  var reduced = angle;
  if (abs(angle) >= 0.000000059604644775390625) {
    let bits = bitcast<u32>(abs(angle));
    let mantissa = (bits & 0x007fffffu) | 0x00800000u;
    let shift = i32((bits >> 23u) & 255u) - 119;
    var input_fixed = 0u;
    if (shift >= 0) { input_fixed = mantissa << u32(shift); }
    else {
      let right = u32(-shift);
      input_fixed = (mantissa + (1u << (right - 1u))) >> right;
    }
    let power_fixed = u32(abs(quadrant)) * 3373259426u;
    let residual = select(power_fixed - input_fixed, input_fixed - power_fixed, angle >= 0.0);
    reduced = f32(bitcast<i32>(residual)) * 0.0000000004656612873077392578125;
  }
  let square = fma(reduced, reduced, 0.0);
  var sp = 1.6059043836821613e-10;
  sp = fma(sp, square, -2.505210838544172e-8);
  sp = fma(sp, square, 2.7557319223985893e-6);
  sp = fma(sp, square, -0.0001984126984126984);
  sp = fma(sp, square, 0.008333333333333333);
  sp = fma(sp, square, -0.16666666666666666);
  let sine = fma(reduced * square, sp, reduced);
  var cp = 2.08767569878681e-9;
  cp = fma(cp, square, -2.755731922398589e-7);
  cp = fma(cp, square, 0.0000248015873015873);
  cp = fma(cp, square, -0.001388888888888889);
  cp = fma(cp, square, 0.041666666666666664);
  cp = fma(cp, square, -0.5);
  let cosine = fma(square, cp, 1.0);
  switch (u32(i32(quadrant) & 3)) {
    case 1u: { return vec2<f32>(-sine, cosine); }
    case 2u: { return vec2<f32>(-cosine, -sine); }
    case 3u: { return vec2<f32>(sine, -cosine); }
    default: { return vec2<f32>(cosine, sine); }
  }
}
@compute @workgroup_size(1) fn main() {buf[0] = sincos_refined(buf[0]).x;}
