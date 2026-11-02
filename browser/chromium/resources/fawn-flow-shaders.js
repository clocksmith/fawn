// Canonical flow solvers shared by CanvasContext and benchmark fixtures.
const WORKGROUP_SIZE = 16;
const PRESSURE_ITERATIONS = 12;
const DRAG_GAIN = 8.0;
const PRESS_IMPULSE = 0.65;
const SWIRL_ACCELERATION = 0.9;
const MAX_FLUID_SPEED = 6.0;
const DYE_RATE = 4.0;
const PRESS_DYE = 0.18;

const SHARED_HELPERS = `
struct Params {
  grid: vec4<f32>,
  mouse: vec4<f32>,
  sim: vec4<f32>,
  pointerDelta: vec4<f32>,
  viewport: vec4<f32>,
}

fn idx_at(x: i32, y: i32) -> u32 {
  let w = i32(u.grid.x);
  let h = i32(u.grid.y);
  let cx = clamp(x, 0, w - 1);
  let cy = clamp(y, 0, h - 1);
  return u32(cy * w + cx);
}

fn uv_to_grid(uv: vec2<f32>) -> vec2<f32> {
  return uv * u.grid.xy - vec2<f32>(0.5, 0.5);
}

fn safe_norm(v: vec2<f32>) -> vec2<f32> {
  return v / max(length(v), 1e-5);
}

fn pointer_falloff(uv: vec2<f32>, radius: f32) -> f32 {
  // A capsule covers the entire stroke, including movement between rendered frames.
  let stroke = u.pointerDelta.xy;
  let start = u.mouse.xy - stroke;
  let along = clamp(dot(uv - start, stroke) / max(dot(stroke, stroke), 1e-8), 0.0, 1.0);
  let distance = length(uv - (start + along * stroke));
  return (1.0 - smoothstep(0.0, radius, distance)) * u.mouse.z;
}

`;

const ADVECT_VEL_WGSL = `
${SHARED_HELPERS}
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage, read> velSrc: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> velDst: array<vec4<f32>>;

fn sample_vel(uv: vec2<f32>) -> vec2<f32> {
  let gridPos = uv_to_grid(clamp(uv, u.grid.zw * 0.5, vec2<f32>(1.0) - u.grid.zw * 0.5));
  let base = floor(gridPos);
  let frac = gridPos - base;
  let x0 = i32(base.x);
  let y0 = i32(base.y);
  let x1 = x0 + 1;
  let y1 = y0 + 1;
  let v00 = velSrc[idx_at(x0, y0)].xy;
  let v10 = velSrc[idx_at(x1, y0)].xy;
  let v01 = velSrc[idx_at(x0, y1)].xy;
  let v11 = velSrc[idx_at(x1, y1)].xy;
  let vx0 = mix(v00, v10, frac.x);
  let vx1 = mix(v01, v11, frac.x);
  return mix(vx0, vx1, frac.y);
}

@compute @workgroup_size(${WORKGROUP_SIZE}, ${WORKGROUP_SIZE})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let width = u32(u.grid.x);
  let height = u32(u.grid.y);
  if (gid.x >= width || gid.y >= height) {
    return;
  }

  let uv = (vec2<f32>(gid.xy) + 0.5) * u.grid.zw;
  let currentVel = sample_vel(uv);
  let prevUv = clamp(uv - currentVel * u.sim.x, u.grid.zw * 0.5, vec2<f32>(1.0) - u.grid.zw * 0.5);
  var vel = sample_vel(prevUv) * pow(0.989, u.sim.x * 60.0 / 0.82);

  let delta = uv - u.mouse.xy;
  let falloff = pointer_falloff(uv, u.mouse.w);
  let drag = u.pointerDelta.xy / u.viewport.z;
  let swirl = vec2<f32>(-delta.y, delta.x);
  // Drag is displacement: consume its impulse once, without another dt factor.
  vel += drag * ${DRAG_GAIN} * falloff;
  vel += (safe_norm(swirl) * ${SWIRL_ACCELERATION} * u.sim.x
    + safe_norm(delta) * ${PRESS_IMPULSE} * u.pointerDelta.z / u.viewport.z) * falloff;
  // Cross-axis currents are divergence-free, so projection preserves the opening motion.
  let openingFlow = vec2<f32>(sin(uv.y * 6.28318), -sin(uv.x * 6.28318))
    + vec2<f32>(sin(uv.y * 18.84954), cos(uv.x * 18.84954)) * 0.35;
  vel += openingFlow * u.pointerDelta.w * u.sim.x;
  vel *= min(1.0, ${MAX_FLUID_SPEED} / max(length(vel), 1e-5));

  if (gid.x <= 1u || gid.y <= 1u || gid.x >= width - 2u || gid.y >= height - 2u) {
    vel *= 0.0;
  }
  velDst[idx_at(i32(gid.x), i32(gid.y))] = vec4<f32>(vel, 0.0, 0.0);
}
`;

const DIV_CURL_WGSL = `
${SHARED_HELPERS}
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage, read> velSrc: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> divCurl: array<vec4<f32>>;

fn vel_at(x: i32, y: i32) -> vec2<f32> {
  return velSrc[idx_at(x, y)].xy;
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
  let l = vel_at(x - 1, y);
  let r = vel_at(x + 1, y);
  let b = vel_at(x, y - 1);
  let t = vel_at(x, y + 1);

  let div = 0.5 * ((r.x - l.x) / u.grid.z + (t.y - b.y) / u.grid.w);
  let curl = 0.5 * ((r.y - l.y) / u.grid.z - (t.x - b.x) / u.grid.w);
  divCurl[idx_at(x, y)] = vec4<f32>(div, curl, abs(curl), 0.0);
}
`;

const PRESSURE_WGSL = `
${SHARED_HELPERS}
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage, read> pressureSrc: array<f32>;
@group(0) @binding(2) var<storage, read> divCurl: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> pressureDst: array<f32>;

fn pressure_at(x: i32, y: i32) -> f32 {
  return pressureSrc[idx_at(x, y)];
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
  let div = divCurl[idx_at(x, y)].x;
  let l = pressure_at(x - 1, y);
  let r = pressure_at(x + 1, y);
  let b = pressure_at(x, y - 1);
  let t = pressure_at(x, y + 1);
  let p = (l + r + b + t - div * 0.85) * 0.25;
  pressureDst[idx_at(x, y)] = p;
}
`;

const PROJECT_WGSL = `
${SHARED_HELPERS}
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage, read> velSrc: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> pressure: array<f32>;
@group(0) @binding(3) var<storage, read> divCurl: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> velDst: array<vec4<f32>>;

fn pressure_at(x: i32, y: i32) -> f32 {
  return pressure[idx_at(x, y)];
}

fn curl_at(x: i32, y: i32) -> f32 {
  return divCurl[idx_at(x, y)].y;
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
  var vel = velSrc[i].xy;
  let grad = vec2<f32>(
    pressure_at(x + 1, y) - pressure_at(x - 1, y),
    pressure_at(x, y + 1) - pressure_at(x, y - 1)
  ) * 0.5;
  vel -= grad * vec2<f32>(u.grid.z, u.grid.w);

  let curl = curl_at(x, y);
  let gradCurl = vec2<f32>(
    abs(curl_at(x + 1, y)) - abs(curl_at(x - 1, y)),
    abs(curl_at(x, y + 1)) - abs(curl_at(x, y - 1))
  );
  let curlLen = max(length(gradCurl), 1e-5);
  let n = gradCurl / curlLen;
  let vorticity = vec2<f32>(n.y, -n.x) * curl * u.sim.z * u.sim.x * 0.026;
  vel += vorticity;

  if (gid.x <= 1u || gid.y <= 1u || gid.x >= width - 2u || gid.y >= height - 2u) {
    vel *= 0.0;
  }
  velDst[i] = vec4<f32>(vel, 0.0, 0.0);
}
`;

const ADVECT_DYE_WGSL = `
${SHARED_HELPERS}
@group(0) @binding(0) var<uniform> u: Params;
@group(0) @binding(1) var<storage, read> dyeSrc: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> velSrc: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> dyeDst: array<vec4<f32>>;

fn sample_vel(uv: vec2<f32>) -> vec2<f32> {
  let gridPos = uv_to_grid(clamp(uv, u.grid.zw * 0.5, vec2<f32>(1.0) - u.grid.zw * 0.5));
  let base = floor(gridPos);
  let frac = gridPos - base;
  let x0 = i32(base.x);
  let y0 = i32(base.y);
  let x1 = x0 + 1;
  let y1 = y0 + 1;
  let v00 = velSrc[idx_at(x0, y0)].xy;
  let v10 = velSrc[idx_at(x1, y0)].xy;
  let v01 = velSrc[idx_at(x0, y1)].xy;
  let v11 = velSrc[idx_at(x1, y1)].xy;
  let vx0 = mix(v00, v10, frac.x);
  let vx1 = mix(v01, v11, frac.x);
  return mix(vx0, vx1, frac.y);
}

fn sample_dye(uv: vec2<f32>) -> vec3<f32> {
  let gridPos = uv_to_grid(clamp(uv, u.grid.zw * 0.5, vec2<f32>(1.0) - u.grid.zw * 0.5));
  let base = floor(gridPos);
  let frac = gridPos - base;
  let x0 = i32(base.x);
  let y0 = i32(base.y);
  let x1 = x0 + 1;
  let y1 = y0 + 1;
  let d00 = dyeSrc[idx_at(x0, y0)].xyz;
  let d10 = dyeSrc[idx_at(x1, y0)].xyz;
  let d01 = dyeSrc[idx_at(x0, y1)].xyz;
  let d11 = dyeSrc[idx_at(x1, y1)].xyz;
  let dx0 = mix(d00, d10, frac.x);
  let dx1 = mix(d01, d11, frac.x);
  return mix(dx0, dx1, frac.y);
}

@compute @workgroup_size(${WORKGROUP_SIZE}, ${WORKGROUP_SIZE})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let width = u32(u.grid.x);
  let height = u32(u.grid.y);
  if (gid.x >= width || gid.y >= height) {
    return;
  }

  let uv = (vec2<f32>(gid.xy) + 0.5) * u.grid.zw;
  let vel = sample_vel(uv);
  let prevUv = clamp(uv - vel * u.sim.x, u.grid.zw * 0.5, vec2<f32>(1.0) - u.grid.zw * 0.5);
  var dye = sample_dye(prevUv) * pow(0.992, u.sim.x * 60.0 / 0.82);

  let falloff = pointer_falloff(uv, u.mouse.w * 0.85);
  let tint = 0.5 + 0.5 * cos(
    vec3<f32>(0.0, 2.1, 4.2)
    + vec3<f32>(
      u.sim.y * 0.28 + uv.x * 8.0,
      u.sim.y * 0.23 + uv.y * 7.0,
      u.sim.y * 0.19 + (uv.x + uv.y) * 5.0
    )
  );
  dye += tint * falloff * (${DYE_RATE} * u.sim.x + ${PRESS_DYE} * u.pointerDelta.z / u.viewport.z);
  dye *= pow(0.996, u.sim.x * 60.0 / 0.82);
  dyeDst[idx_at(i32(gid.x), i32(gid.y))] = vec4<f32>(dye, 1.0);
}
`;
export { WORKGROUP_SIZE, PRESSURE_ITERATIONS, DRAG_GAIN, PRESS_IMPULSE, SWIRL_ACCELERATION, MAX_FLUID_SPEED, DYE_RATE, PRESS_DYE, SHARED_HELPERS, ADVECT_VEL_WGSL, DIV_CURL_WGSL, PRESSURE_WGSL, PROJECT_WGSL, ADVECT_DYE_WGSL };
