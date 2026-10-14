// Magnets: an iridescent ferrofluid film raised into spikes by drifting magnets and soap-bubble
// rings. Holding the pointer adds a magnet and charges it; releasing sends the charge out as a ring.
import { CELL_WORKGROUP, FRAME_WGSL, fieldSampler, sheetWgsl } from './fawn-gpu.js';

const MAGNET_POLICY = Object.freeze({
  timeScale: 1.1,
  magnets: 15,
  bubbles: 9,
  sourceMotion: 3,
  hoverForce: 0.55 / 3,
  heldForce: 0.13,
  ringWidth: 0.03,
  ringHeight: 0.6,
  pigmentMix: 0.32,
  carriedGlow: 0.06,
  carriedRidge: 0.6,
  carriedFlow: 0.004,
  body: Object.freeze([0.003, 0.004, 0.006]),
  parcelRadiusCells: 1.4,
});
const M = MAGNET_POLICY;
const GOLDEN_ANGLE = 2.399963;

const COMMON = /* wgsl */ `${FRAME_WGSL}
fn magnetDt() -> f32 { return frame.clock.x * ${M.timeScale}; }
fn reliefSlope(l: f32, r: f32, b: f32, t: f32) -> vec2<f32> {
  // Derivatives in sheet coordinates keep reflections consistent across grid sizes.
  return vec2<f32>(r - l, t - b) * frame.grid.xy * 0.5;
}
fn filmPrism(height: f32, field: f32, tint: vec3<f32>, shift: f32) -> vec3<f32> {
  let phase = height * 0.34 + log(1.0 + max(field, 0.0)) * 0.14 + shift + frame.clock.y * 0.015;
  return mix(spectral(phase), hue(tint, 0.001), ${M.pigmentMix});
}
`;
const CELL_MAIN = /* wgsl */ `@compute @workgroup_size(${CELL_WORKGROUP}, ${CELL_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (outsideGrid(id.xy)) { return; }
  let p = vec2<i32>(id.xy);`;

const STEP = /* wgsl */ `${COMMON}
struct Sources { magnets: array<vec4<f32>, ${M.magnets}>, bubbles: array<vec4<f32>, ${M.bubbles}> }
@group(0) @binding(1) var<uniform> sources: Sources;
@group(0) @binding(2) var<storage, read> surfaceIn: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> surfaceOut: array<vec4<f32>>;
fn magneticField(uv: vec2<f32>) -> f32 {
  var field = 0.0;
  for (var i = 0u; i < ${M.magnets}u; i++) {
    let delta = uv - sources.magnets[i].xy;
    field += sources.magnets[i].z / max(dot(delta, delta), sources.magnets[i].w);
  }
  // The pointer is one more magnet until its release hands the charge to the burst ring.
  let pull = pointerInput(${M.hoverForce}, ${M.heldForce}) * select(1.0, 0.0, frame.burst.w > 0.0);
  let delta = uv - frame.pointer.xy;
  return field + pull * 0.24 / max(dot(delta, delta), 0.00045);
}
fn rings(uv: vec2<f32>) -> f32 {
  var height = 0.0;
  for (var i = 0u; i < ${M.bubbles}u; i++) {
    let bubble = sources.bubbles[i];
    height += (1.0 - smoothstep(0.0, 0.018, abs(length(uv - bubble.xy) - bubble.z))) * bubble.w;
  }
  let release = abs(length(uv - frame.burst.xy) - frame.burst.z);
  return height + (1.0 - smoothstep(0.0, ${M.ringWidth}, release)) * frame.burst.w * ${M.ringHeight};
}
${CELL_MAIN}
  let state = surfaceIn[cell(p)];
  let around = surfaceIn[cell(p - vec2<i32>(1, 0))].x + surfaceIn[cell(p + vec2<i32>(1, 0))].x
    + surfaceIn[cell(p - vec2<i32>(0, 1))].x + surfaceIn[cell(p + vec2<i32>(0, 1))].x;
  let uv = cellUv(id.xy);
  let field = magneticField(uv);
  let bubbles = rings(uv);
  let rest = pow(clamp(field * 0.085, 0.0, 1.7), 2.3) * 0.72 + bubbles * 0.34;
  let dt = magnetDt();
  let speed = state.y + ((around - 4.0 * state.x) * 0.30 + (rest - state.x) * 0.74 + bubbles * 0.16
    - state.y * 0.16) * dt * 60.0;
  let response = 1.0 - exp(-12.0 * dt);
  let height = clamp(mix(state.x + speed * dt * 2.1, rest, response), -0.2, 1.6);
  surfaceOut[cell(p)] = vec4<f32>(height, speed, mix(state.z, 0.0, response),
    mix(state.w, field + bubbles * 3.0, response));
}`;

const RENDER = /* wgsl */ `${COMMON}${sheetWgsl(1)}
@group(0) @binding(2) var<storage, read> surface: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> pigment: array<vec4<f32>>;
${fieldSampler('surfaceAt', 'surface')}
${fieldSampler('pigmentAt', 'pigment', 'vec3<f32>', '.rgb')}
@fragment fn fs(in: SheetOut) -> @location(0) vec4<f32> {
  let c = surfaceAt(in.uv);
  let dx = vec2<f32>(frame.grid.z, 0.0);
  let dy = vec2<f32>(0.0, frame.grid.w);
  let relief = reliefSlope(surfaceAt(in.uv - dx).x, surfaceAt(in.uv + dx).x,
    surfaceAt(in.uv - dy).x, surfaceAt(in.uv + dy).x);
  // The carried sheet has a real silhouette edge as well as its bubble relief.
  let edge = reliefSlope(coverage(in.placed - dx), coverage(in.placed + dx),
    coverage(in.placed - dy), coverage(in.placed + dy));
  let slope = relief + edge * 0.16;
  let normal = normalize(vec3<f32>(-slope * 0.12, 1.0));
  let fresnel = pow(1.0 - max(normal.z, 0.0), 3.0);
  let ridge = smoothstep(0.20, 4.0, length(slope));
  let prism = filmPrism(c.x, c.w, pigmentAt(in.uv), fresnel * 0.55 + dot(normal.xy, vec2<f32>(0.4, -0.3)));
  let contour = pow(0.5 + 0.5 * sin(log(1.0 + max(c.w, 0.0)) * 8.0 - c.x * 2.0), 12.0);
  let halfVector = normalize(normalize(vec3<f32>(0.45, 0.60, 1.0)) + vec3<f32>(0.0, 0.0, 1.0));
  let specular = pow(max(dot(normal, halfVector), 0.0), 64.0) * ridge;
  let film = prism * ridge * (0.02 + contour * 0.40 + fresnel * 0.16) + vec3<f32>(0.80, 0.88, 1.0) * specular * 0.28;
  return vec4<f32>(vec3<f32>(${M.body.join(', ')}) + film * coverage(in.placed), 1.0);
}`;

const EXPORT = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> surface: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> pigment: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> carryColor: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> carryMotion: array<vec4<f32>>;
${CELL_MAIN}
  let c = surface[cell(p)];
  let slope = reliefSlope(surface[cell(p - vec2<i32>(1, 0))].x, surface[cell(p + vec2<i32>(1, 0))].x,
    surface[cell(p - vec2<i32>(0, 1))].x, surface[cell(p + vec2<i32>(0, 1))].x);
  // Carried light follows the film's ridges; its motion runs along the height contours.
  let light = ${M.carriedGlow} + ${M.carriedRidge} * smoothstep(0.20, 4.0, length(slope));
  carryColor[cell(p)] = vec4<f32>(filmPrism(c.x, c.w, pigment[cell(p)].rgb, 0.0) * light, 1.0);
  carryMotion[cell(p)] = vec4<f32>(vec2<f32>(-slope.y, slope.x) * ${M.carriedFlow}, 0.0, 0.0);
}`;

const IMPORT = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> carryColor: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> carryMotion: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> surface: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> pigment: array<vec4<f32>>;
${CELL_MAIN}
  // Bright incoming material starts as raised film, so the first spikes follow its shapes.
  let color = carryColor[cell(p)].rgb;
  let brightness = luminance(color);
  surface[cell(p)] = vec4<f32>(clamp(brightness * 0.65, 0.0, 1.6), length(carryMotion[cell(p)].xy) * 0.08,
    0.0, brightness * 12.0);
  pigment[cell(p)] = vec4<f32>(color, 1.0);
}`;

function writeSources(data, time) {
  const t = time * M.sourceMotion;
  for (let i = 0; i < M.magnets; i++) {
    const phase = i * GOLDEN_ANGLE;
    data.set([
      0.12 + (i % 5) * 0.19 + Math.sin(t * (0.17 + (i % 4) * 0.04) + phase) * 0.08,
      0.18 + Math.floor(i / 5) * 0.31 + Math.cos(t * (0.19 + (i % 3) * 0.05) + phase) * 0.1,
      0.028 + (i % 3) * 0.005,
      0.0006,
    ], i * 4);
  }
  for (let i = 0; i < M.bubbles; i++) {
    const phase = i * GOLDEN_ANGLE;
    data.set([
      0.16 + (i % 3) * 0.34 + Math.sin(t * (0.18 + (i % 3) * 0.025) + phase) * 0.1,
      0.16 + Math.floor(i / 3) * 0.34 + Math.cos(t * (0.16 + (i % 4) * 0.035) + phase) * 0.1,
      0.035 + 0.055 * (0.5 + 0.5 * Math.sin(t * (0.8 + (i % 3) * 0.15) + phase)),
      0.5 + (i % 3) * 0.08,
    ], (M.magnets + i) * 4);
  }
}

function createMagnets({ kit, grid, frame, logoMask, carry }) {
  const cells = grid * grid;
  const surface = [kit.storage('Magnets surface 0', cells * 16), kit.storage('Magnets surface 1', cells * 16)];
  const pigment = kit.storage('Magnets pigment', cells * 16);
  const sourceData = new Float32Array((M.magnets + M.bubbles) * 4);
  const sources = kit.uniform('Magnets sources', sourceData.byteLength);
  const pipelines = {
    step: kit.compute('Magnets film', STEP),
    exportCarry: kit.compute('Magnets export', EXPORT),
    importCarry: kit.compute('Magnets import', IMPORT),
    render: kit.render('Magnets material', RENDER, { vertex: 'sheetVertex' }),
  };
  const pair = (make) => [0, 1].map(make);
  const groups = {
    step: pair((i) => kit.bind(pipelines.step, [frame, sources, surface[i], surface[1 - i]])),
    render: pair((i) => kit.bind(pipelines.render, [frame, logoMask, surface[i], pigment])),
    exportCarry: pair((i) => carry.map((slot) =>
      kit.bind(pipelines.exportCarry, [frame, surface[i], pigment, slot.color, slot.motion]))),
    importCarry: carry.map((slot) =>
      kit.bind(pipelines.importCarry, [frame, slot.color, slot.motion, surface[0], pigment])),
  };
  let current = 0;
  const run = (pass, pipeline, group) => kit.dispatchCells(pass, pipeline, group, grid);
  return {
    background: M.body,
    parcelRadius: () => (M.parcelRadiusCells * 2) / grid,
    simulate(pass, { steps, time }) {
      writeSources(sourceData, time);
      kit.device.queue.writeBuffer(sources, 0, sourceData);
      for (let step = 0; step < steps; step++) {
        run(pass, pipelines.step, groups.step[current]);
        current = 1 - current;
      }
    },
    render(encoder, view) {
      kit.drawScreen(encoder, view, pipelines.render, groups.render[current], M.body);
    },
    exportCarry(encoder, slot) {
      const pass = encoder.beginComputePass({ label: 'Magnets export' });
      run(pass, pipelines.exportCarry, groups.exportCarry[current][slot]);
      pass.end();
    },
    importCarry(encoder, slot) {
      const pass = encoder.beginComputePass({ label: 'Magnets import' });
      run(pass, pipelines.importCarry, groups.importCarry[slot]);
      pass.end();
      current = 0;
    },
  };
}

export { MAGNET_POLICY, createMagnets };
