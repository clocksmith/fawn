// Canonical wave solvers shared by CanvasContext and benchmark fixtures.
const WORKGROUP_SIZE = 16;
const WAVE_ACTIVITY = 3;
const WAVE_RELAXATION_RATE = 12;
const MAGNET_COUNT = 5 * WAVE_ACTIVITY;
const BUBBLE_COUNT = 3 * WAVE_ACTIVITY;
const WAVE_UNIFORM_FLOATS = 12 + 4 * (MAGNET_COUNT + BUBBLE_COUNT);

// Animate each source once per frame, rather than repeating trigonometry per cell.
function updateWaveSources(data, time, strength = 1, spinRate = 1) {
  const motionTime = time * WAVE_ACTIVITY * spinRate;
  for (let i = 0; i < MAGNET_COUNT; i++) {
    const phase = i * 2.399963;
    const offset = 12 + i * 4;
    data[offset] = 0.12 + (i % 5) * 0.19 + Math.sin(motionTime * (0.17 + (i % 4) * 0.04) + phase) * 0.08;
    data[offset + 1] = 0.18 + Math.floor(i / 5) * 0.31 + Math.cos(motionTime * (0.19 + (i % 3) * 0.05) + phase) * 0.10;
    data[offset + 2] = (0.028 + (i % 3) * 0.005) * strength;
    data[offset + 3] = 0.0006;
  }
  for (let i = 0; i < BUBBLE_COUNT; i++) {
    const phase = i * 2.399963;
    const offset = 12 + (MAGNET_COUNT + i) * 4;
    data[offset] = 0.16 + (i % 3) * 0.34 + Math.sin(motionTime * (0.18 + (i % 3) * 0.025) + phase) * 0.10;
    data[offset + 1] = 0.16 + Math.floor(i / 3) * 0.34 + Math.cos(motionTime * (0.16 + (i % 4) * 0.035) + phase) * 0.10;
    data[offset + 2] = 0.035 + 0.055 * (0.5 + 0.5 * Math.sin(motionTime * (0.8 + (i % 3) * 0.15) + phase));
    data[offset + 3] = (0.50 + (i % 3) * 0.08) * strength;
  }

}

const COMPUTE_WGSL = `
struct Params {
  grid: vec4<f32>,
  mouse: vec4<f32>,
  sim: vec4<f32>,
  magnets: array<vec4<f32>, ${MAGNET_COUNT}>,
  bubbles: array<vec4<f32>, ${BUBBLE_COUNT}>,
}

@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage, read> src: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> dst: array<vec4<f32>>;

fn idx_at(x: i32, y: i32) -> u32 {
  let w = i32(u.grid.x);
  let h = i32(u.grid.y);
  let cx = clamp(x, 0, w - 1);
  let cy = clamp(y, 0, h - 1);
  return u32(cy * w + cx);
}

fn sample_height(x: i32, y: i32) -> f32 {
  return src[idx_at(x, y)].x;
}

fn magnetic_field(uv: vec2<f32>) -> f32 {
  var field = 0.0;
  for (var i = 0u; i < ${MAGNET_COUNT}u; i += 1u) {
    let source = u.magnets[i];
    let delta = uv - source.xy;
    field += source.z / max(dot(delta, delta), source.w);
  }
  let delta = uv - u.mouse.xy;
  field += u.mouse.z * 0.24 / max(dot(delta, delta), 0.00045);
  return field;
}

fn bubble_field(uv: vec2<f32>) -> f32 {
  var bubbles = 0.0;
  for (var i = 0u; i < ${BUBBLE_COUNT}u; i += 1u) {
    let source = u.bubbles[i];
    let ringDistance = abs(length(uv - source.xy) - source.z);
    bubbles += (1.0 - smoothstep(0.0, 0.018, ringDistance)) * source.w;
  }
  return bubbles;
}

@compute @workgroup_size(${WORKGROUP_SIZE}, ${WORKGROUP_SIZE})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let width = u32(u.grid.x);
  let height = u32(u.grid.y);
  if (gid.x >= width || gid.y >= height) {
    return;
  }

  let x = i32(gid.x);
  let y = i32(gid.y);
  let i = idx_at(x, y);
  let state = src[i];
  let h = state.x;
  let v = state.y;
  let uv = (vec2<f32>(gid.xy) + 0.5) * u.grid.zw;

  let l = sample_height(x - 1, y);
  let r = sample_height(x + 1, y);
  let b = sample_height(x, y - 1);
  let t = sample_height(x, y + 1);
  let lap = (l + r + b + t - 4.0 * h);

  let field = magnetic_field(uv);
  let bubbles = bubble_field(uv);
  let spikes = pow(clamp(field * 0.085, 0.0, 1.7), 2.3);
  let targetHeight = spikes * 0.72 + bubbles * 0.34;

  var nextV = v + (lap * 0.30 + (targetHeight - h) * 0.74 + bubbles * 0.16 - v * 0.16) * u.sim.x * 60.0;
  var nextH = h + nextV * u.sim.x * 2.1;
  nextH = mix(nextH, targetHeight, 1.0 - exp(-${WAVE_RELAXATION_RATE}.0 * u.sim.x));
  nextH = clamp(nextH, -0.2, 1.6);
  let response = 1.0 - exp(-${WAVE_RELAXATION_RATE}.0 * u.sim.x);
  let glow = mix(state.z, 0.0, response);
  let reflection = mix(state.w, field + bubbles * 3.0, response);
  dst[i] = vec4<f32>(nextH, nextV, glow, reflection);
}
`;
export { WORKGROUP_SIZE, WAVE_ACTIVITY, WAVE_RELAXATION_RATE, MAGNET_COUNT, BUBBLE_COUNT, WAVE_UNIFORM_FLOATS, COMPUTE_WGSL, updateWaveSources };
