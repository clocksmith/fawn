// Flows: incompressible prismatic dye with vorticity confinement. Dragging stirs and paints
// along the stroke; drifting emitters keep the fluid alive between gestures.
import { CELL_WORKGROUP, FRAME_WGSL, SHEET_WGSL, fieldSampler } from './fawn-gpu.js';

const FLOW_POLICY = Object.freeze({
  timeScale: 0.82,
  pressureIterations: 12,
  vorticity: 2,
  speedLimit: 6,
  velocityRetention: 0.989,
  dyeRetention: 0.992,
  dyeDecay: 0.996,
  hoverForce: 0.28 / 3,
  heldForce: 0.7,
  hoverRadius: 0.05,
  heldRadius: 0.1,
  dragGain: 8,
  pressImpulse: 0.65,
  swirl: 0.9,
  dyeRate: 4,
  pressDye: 0.18,
  emitters: 3,
  emitterOrbit: 0.3,
  emitterRate: 0.35,
  emitterRadius: 0.07,
  emitterForce: 0.22,
  emitterSpin: 12,
  contrast: 0.18,
  exposure: 1.65,
  background: Object.freeze([0.04, 0.055, 0.07]),
  parcelRadiusCells: 1.4,
  wallFade: Object.freeze([1.5, 5]),
});
const P = FLOW_POLICY;
if (P.pressureIterations % 2) throw new Error('Flow pressure iterations must be even');

const COMMON = /* wgsl */ `${FRAME_WGSL}
fn flowDt() -> f32 { return frame.clock.x * ${P.timeScale}; }
fn steps() -> f32 { return frame.clock.z; }
fn retention(rate: f32) -> f32 { return pow(rate, frame.clock.x * 60.0); }
fn strokeRadius() -> f32 { return mix(${P.hoverRadius}, ${P.heldRadius}, frame.input.y); }
fn strokeFalloff(uv: vec2<f32>, radius: f32) -> f32 {
  // A capsule covers the whole stroke, including pointer travel between rendered frames.
  let stroke = frame.pointer.zw;
  let start = frame.pointer.xy - stroke;
  let along = clamp(dot(uv - start, stroke) / max(dot(stroke, stroke), 1e-8), 0.0, 1.0);
  let gap = length(uv - (start + along * stroke));
  return (1.0 - smoothstep(0.0, radius, gap)) * pointerInput(${P.hoverForce}, ${P.heldForce});
}
// Emitters orbit slowly on Lissajous paths: xy is the position, zw its velocity.
fn emitter(k: u32) -> vec4<f32> {
  let rate = vec2<f32>(0.37 + f32(k) * 0.11, 0.29 + f32(k) * 0.13) * ${P.emitterRate};
  let phase = f32(k) * 2.399963 + vec2<f32>(0.0, 1.3);
  let angle = frame.clock.y * rate + phase;
  return vec4<f32>(0.5 + ${P.emitterOrbit} * vec2<f32>(sin(angle.x), cos(angle.y)),
    ${P.emitterOrbit} * rate * vec2<f32>(cos(angle.x), -sin(angle.y)));
}
fn emitterFalloff(uv: vec2<f32>, e: vec4<f32>) -> f32 {
  return (1.0 - smoothstep(0.0, ${P.emitterRadius}, length(uv - e.xy))) * ${P.emitterForce};
}
fn isWall(id: vec2<u32>) -> bool {
  let last = u32(frame.grid.x) - 2u;
  return id.x <= 1u || id.y <= 1u || id.x >= last || id.y >= last;
}
`;
const CELL_MAIN = /* wgsl */ `@compute @workgroup_size(${CELL_WORKGROUP}, ${CELL_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (outsideGrid(id.xy)) { return; }
  let p = vec2<i32>(id.xy);`;
const carryBindings = (first, access) => /* wgsl */ `
@group(0) @binding(${first}) var<storage, ${access}> carryColor: array<vec4<f32>>;
@group(0) @binding(${first + 1}) var<storage, ${access}> carryMotion: array<vec4<f32>>;`;

const ADVECT_VELOCITY = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> velocityOut: array<vec4<f32>>;
${fieldSampler('velocityAt', 'velocityIn', 'vec2<f32>', '.xy')}
${CELL_MAIN}
  let uv = cellUv(id.xy);
  var v = velocityAt(uv - velocityAt(uv) * flowDt()) * retention(${P.velocityRetention});
  let delta = uv - frame.pointer.xy;
  let falloff = strokeFalloff(uv, strokeRadius());
  // Drag is a displacement, so its impulse is spread over the frame's steps instead of scaled by dt.
  v += frame.pointer.zw / steps() * ${P.dragGain} * falloff;
  v += (safeNormalize(vec2<f32>(-delta.y, delta.x)) * ${P.swirl} * flowDt()
    + safeNormalize(delta) * ${P.pressImpulse} * frame.input.z / steps()) * falloff;
  for (var k = 0u; k < ${P.emitters}u; k++) {
    let e = emitter(k);
    let around = uv - e.xy;
    // Solid-body spin has no singular center, so emitters never imprint the grid.
    v += (e.zw + vec2<f32>(-around.y, around.x) * ${P.emitterSpin}) * flowDt() * emitterFalloff(uv, e);
  }
  v *= min(1.0, ${P.speedLimit} / max(length(v), 1e-5));
  velocityOut[cell(p)] = vec4<f32>(select(v, vec2<f32>(0.0), isWall(id.xy)), 0.0, 0.0);
}`;

const DIVERGENCE_CURL = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> velocity: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> divergenceCurl: array<vec4<f32>>;
${CELL_MAIN}
  let l = velocity[cell(p - vec2<i32>(1, 0))].xy;
  let r = velocity[cell(p + vec2<i32>(1, 0))].xy;
  let b = velocity[cell(p - vec2<i32>(0, 1))].xy;
  let t = velocity[cell(p + vec2<i32>(0, 1))].xy;
  let divergence = 0.5 * ((r.x - l.x) + (t.y - b.y)) * frame.grid.x;
  let curl = 0.5 * ((r.y - l.y) - (t.x - b.x)) * frame.grid.x;
  divergenceCurl[cell(p)] = vec4<f32>(divergence, curl, abs(curl), 0.0);
}`;

const PRESSURE = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> pressureIn: array<f32>;
@group(0) @binding(2) var<storage, read> divergenceCurl: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> pressureOut: array<f32>;
${CELL_MAIN}
  let around = pressureIn[cell(p - vec2<i32>(1, 0))] + pressureIn[cell(p + vec2<i32>(1, 0))]
    + pressureIn[cell(p - vec2<i32>(0, 1))] + pressureIn[cell(p + vec2<i32>(0, 1))];
  pressureOut[cell(p)] = (around - divergenceCurl[cell(p)].x * 0.85) * 0.25;
}`;

const PROJECT = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> pressure: array<f32>;
@group(0) @binding(3) var<storage, read> divergenceCurl: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> velocityOut: array<vec4<f32>>;
${CELL_MAIN}
  let gradient = vec2<f32>(pressure[cell(p + vec2<i32>(1, 0))] - pressure[cell(p - vec2<i32>(1, 0))],
    pressure[cell(p + vec2<i32>(0, 1))] - pressure[cell(p - vec2<i32>(0, 1))]) * 0.5;
  var v = velocityIn[cell(p)].xy - gradient * frame.grid.zw;
  // Vorticity confinement restores the swirls a coarse grid would otherwise smear away.
  let swirl = safeNormalize(vec2<f32>(
    divergenceCurl[cell(p + vec2<i32>(1, 0))].z - divergenceCurl[cell(p - vec2<i32>(1, 0))].z,
    divergenceCurl[cell(p + vec2<i32>(0, 1))].z - divergenceCurl[cell(p - vec2<i32>(0, 1))].z));
  v += vec2<f32>(swirl.y, -swirl.x) * divergenceCurl[cell(p)].y * ${P.vorticity} * flowDt() * 0.026;
  velocityOut[cell(p)] = vec4<f32>(select(v, vec2<f32>(0.0), isWall(id.xy)), 0.0, 0.0);
}`;

const ADVECT_DYE = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> dyeIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> velocity: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> dyeOut: array<vec4<f32>>;
${fieldSampler('velocityAt', 'velocity', 'vec2<f32>', '.xy')}
${fieldSampler('dyeAt', 'dyeIn', 'vec3<f32>', '.xyz')}
${CELL_MAIN}
  let uv = cellUv(id.xy);
  let time = frame.clock.y;
  var dye = dyeAt(uv - velocityAt(uv) * flowDt()) * retention(${P.dyeRetention});
  let tint = 0.5 + 0.5 * cos(vec3<f32>(0.0, 2.1, 4.2) + vec3<f32>(time * 0.28 + uv.x * 8.0,
    time * 0.23 + uv.y * 7.0, time * 0.19 + (uv.x + uv.y) * 5.0));
  var paint = strokeFalloff(uv, strokeRadius() * 0.85)
    * (${P.dyeRate} * flowDt() + ${P.pressDye} * frame.input.z / steps());
  for (var k = 0u; k < ${P.emitters}u; k++) {
    paint += emitterFalloff(uv, emitter(k)) * ${P.dyeRate} * flowDt();
  }
  dyeOut[cell(p)] = vec4<f32>((dye + tint * paint) * retention(${P.dyeDecay}), 1.0);
}`;

const RENDER = /* wgsl */ `${COMMON}${SHEET_WGSL}
@group(0) @binding(1) var<storage, read> dye: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> velocity: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> divergenceCurl: array<vec4<f32>>;
fn relief(d: vec3<f32>, v: vec2<f32>, curl: f32) -> f32 {
  return clamp(length(d) * 0.64 + abs(curl) * 0.016 + length(v) * 0.10, 0.0, 1.3);
}
@fragment fn fs(in: SheetOut) -> @location(0) vec4<f32> {
  let at = clamp(in.uv * frame.grid.xy - 0.5, vec2<f32>(0.0), frame.grid.xy - 1.0);
  let b = vec2<i32>(floor(at));
  let f = at - floor(at);
  // One set of four corner samples feeds color, relief, and local contrast.
  let i00 = cell(b);
  let i10 = cell(b + vec2<i32>(1, 0));
  let i01 = cell(b + vec2<i32>(0, 1));
  let i11 = cell(b + vec2<i32>(1, 1));
  let d00 = dye[i00].rgb; let d10 = dye[i10].rgb; let d01 = dye[i01].rgb; let d11 = dye[i11].rgb;
  let v00 = velocity[i00].xy; let v10 = velocity[i10].xy;
  let v01 = velocity[i01].xy; let v11 = velocity[i11].xy;
  let c00 = divergenceCurl[i00].y; let c10 = divergenceCurl[i10].y;
  let c01 = divergenceCurl[i01].y; let c11 = divergenceCurl[i11].y;
  let color = mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
  let speed = length(mix(mix(v00, v10, f.x), mix(v01, v11, f.x), f.y));
  let curl = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
  // Interpolate height derivatives instead of stepping normals at cell borders.
  let h00 = relief(d00, v00, c00); let h10 = relief(d10, v10, c10);
  let h01 = relief(d01, v01, c01); let h11 = relief(d11, v11, c11);
  let height = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
  let slope = vec2<f32>(mix(h10 - h00, h11 - h01, f.y), mix(h01 - h00, h11 - h10, f.x));
  let crisp = max(vec3<f32>(0.0), color + (color - (d00 + d10 + d01 + d11) * 0.25) * ${P.contrast});
  let normal = normalize(vec3<f32>(-slope * 1.25, 1.0));
  let fresnel = pow(1.0 - max(normal.z, 0.0), 3.0);
  let prism = spectral(height * 0.22 + abs(curl) * 0.36 + frame.clock.y * 0.020);
  let secondary = spectral(speed * 0.30 + normal.x * 0.15 - normal.y * 0.08);
  let glint = pow(max(reflect(normalize(vec3<f32>(0.20, -0.16, -1.0)), normal).z, 0.0), 20.0);
  let glow = crisp * (0.92 + speed * 0.24) + prism * abs(curl) * 0.030;
  let metallic = mix(vec3<f32>(0.05, 0.06, 0.08), prism, 0.52) + secondary * fresnel * 0.22
    + vec3<f32>(1.0, 0.96, 0.93) * glint * 0.34;
  let lit = tone(glow + metallic * (0.22 + height * 0.14), ${P.exposure});
  let background = vec3<f32>(${P.background.join(', ')});
  // No-slip walls hold stagnant dye, so the sheet fades out over its outermost cells.
  let inset = min(min(at.x, at.y), min(frame.grid.x - 1.0 - at.x, frame.grid.y - 1.0 - at.y));
  let wall = smoothstep(${P.wallFade[0]}, ${P.wallFade[1]}, inset);
  return vec4<f32>(mix(background, max(lit, background), wall), 1.0);
}`;

const EXPORT = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> dye: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> velocity: array<vec4<f32>>;
${carryBindings(3, 'read_write')}
${CELL_MAIN}
  carryColor[cell(p)] = vec4<f32>(dye[cell(p)].rgb, 1.0);
  carryMotion[cell(p)] = vec4<f32>(velocity[cell(p)].xy, 0.0, 0.0);
}`;

const IMPORT = /* wgsl */ `${COMMON}
${carryBindings(1, 'read')}
@group(0) @binding(3) var<storage, read_write> dye: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> velocity: array<vec4<f32>>;
${CELL_MAIN}
  dye[cell(p)] = vec4<f32>(carryColor[cell(p)].rgb, 1.0);
  velocity[cell(p)] = vec4<f32>(carryMotion[cell(p)].xy, 0.0, 0.0);
}`;

function createFlow({ kit, grid, frame, carry }) {
  const cells = grid * grid;
  const field = (label) => kit.storage(`Flow ${label}`, cells * 16);
  const velocity = [field('velocity 0'), field('velocity 1')];
  const dye = [field('dye 0'), field('dye 1')];
  const pressure = [kit.storage('Flow pressure 0', cells * 4), kit.storage('Flow pressure 1', cells * 4)];
  const divergenceCurl = field('divergence and curl');
  const pipelines = {
    advectVelocity: kit.compute('Flow advect velocity', ADVECT_VELOCITY),
    divergenceCurl: kit.compute('Flow divergence and curl', DIVERGENCE_CURL),
    pressure: kit.compute('Flow pressure', PRESSURE),
    project: kit.compute('Flow projection', PROJECT),
    advectDye: kit.compute('Flow advect dye', ADVECT_DYE),
    exportCarry: kit.compute('Flow export', EXPORT),
    importCarry: kit.compute('Flow import', IMPORT),
    render: kit.render('Flow material', RENDER, { vertex: 'sheetVertex' }),
  };
  const pair = (make) => [0, 1].map(make);
  const groups = {
    advectVelocity: kit.bind(pipelines.advectVelocity, [frame, velocity[0], velocity[1]]),
    divergenceCurl: kit.bind(pipelines.divergenceCurl, [frame, velocity[1], divergenceCurl]),
    pressure: pair((i) => kit.bind(pipelines.pressure, [frame, pressure[i], divergenceCurl, pressure[1 - i]])),
    project: kit.bind(pipelines.project, [frame, velocity[1], pressure[0], divergenceCurl, velocity[0]]),
    advectDye: pair((i) => kit.bind(pipelines.advectDye, [frame, dye[i], velocity[0], dye[1 - i]])),
    render: pair((i) => kit.bind(pipelines.render, [frame, dye[i], velocity[0], divergenceCurl])),
    // Indexed [current dye][carry slot].
    exportCarry: pair((i) => carry.map((slot) =>
      kit.bind(pipelines.exportCarry, [frame, dye[i], velocity[0], slot.color, slot.motion]))),
    importCarry: carry.map((slot) =>
      kit.bind(pipelines.importCarry, [frame, slot.color, slot.motion, dye[0], velocity[0]])),
  };
  const seed = FawnFormation.seed(grid);
  dye.forEach((buffer) => kit.device.queue.writeBuffer(buffer, 0, seed.dye));
  velocity.forEach((buffer) => kit.device.queue.writeBuffer(buffer, 0, seed.velocity));
  let current = 0;
  const run = (pass, name, group = groups[name]) => kit.dispatchCells(pass, pipelines[name], group, grid);
  return {
    background: P.background,
    parcelRadius: () => (P.parcelRadiusCells * 2) / grid,
    simulate(pass, { steps }) {
      for (let step = 0; step < steps; step++) {
        run(pass, 'advectVelocity');
        run(pass, 'divergenceCurl');
        for (let i = 0; i < P.pressureIterations; i++) run(pass, 'pressure', groups.pressure[i % 2]);
        run(pass, 'project');
        run(pass, 'advectDye', groups.advectDye[current]);
        current = 1 - current;
      }
    },
    render(encoder, view) {
      kit.drawScreen(encoder, view, pipelines.render, groups.render[current], P.background);
    },
    exportCarry(encoder, slot) {
      const pass = encoder.beginComputePass({ label: 'Flow export' });
      run(pass, 'exportCarry', groups.exportCarry[current][slot]);
      pass.end();
    },
    importCarry(encoder, slot) {
      [...pressure, divergenceCurl].forEach((buffer) => encoder.clearBuffer(buffer));
      const pass = encoder.beginComputePass({ label: 'Flow import' });
      run(pass, 'importCarry', groups.importCarry[slot]);
      pass.end();
      current = 0;
    },
  };
}

export { FLOW_POLICY, createFlow };
