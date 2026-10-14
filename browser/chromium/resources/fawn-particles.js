// Particles: a glowing point cloud riding a circulating stream field, drawn with fading trails.
// Hovering draws particles in; pressing pushes them away.
import { CELL_WORKGROUP, FRAME_WGSL, ITEM_WORKGROUP, SCREEN_VERTEX_WGSL } from './fawn-gpu.js';

const PARTICLE_POLICY = Object.freeze({
  strideBytes: 32,
  speed: 12,
  speedLimit: 1.75,
  edge: 0.98,
  bounce: 0.92,
  fadeStart: 0.82,
  fadeEnd: 0.96,
  hoverPull: 0.667,
  heldPush: -3.56,
  radiusPx: 1.6,
  radiusRangePx: Object.freeze([0.65, 2]),
  referenceCount: 65536,
  colorGain: 2,
  trailRetention: 0.55,
  importSpeedLimit: 0.18,
  darkest: 0.004,
  fixedPoint: 1024,
  background: Object.freeze([0.010, 0.016, 0.013]),
  blend: Object.freeze({
    color: Object.freeze({ srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }),
    alpha: Object.freeze({ srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' }),
  }),
});
const P = PARTICLE_POLICY;

const COMMON = /* wgsl */ `${FRAME_WGSL}
struct Particle { pos: vec2<f32>, vel: vec2<f32>, color: vec4<f32> }
struct Look { size: f32, columns: f32, rows: f32, unused: f32 }
// Stream-function derivatives circulate tangent to the square sheet's edges.
fn circulation(pos: vec2<f32>) -> vec2<f32> {
  let t = frame.clock.y;
  let angle = pos * 1.5707963;
  let wave = pos * 2.0 + vec2<f32>(t * 0.2, -t * 0.16);
  let modulation = 1.0 + 0.35 * sin(wave.x) * sin(wave.y);
  return vec2<f32>(
    cos(angle.x) * (-1.5707963 * sin(angle.y) * modulation + cos(angle.y) * 0.7 * sin(wave.x) * cos(wave.y)),
    cos(angle.y) * (1.5707963 * sin(angle.x) * modulation - cos(angle.x) * 0.7 * cos(wave.x) * sin(wave.y))
  ) * 0.045;
}
`;

const STEP = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> particlesIn: array<Particle>;
@group(0) @binding(2) var<storage, read_write> particlesOut: array<Particle>;
fn velocityAt(pos: vec2<f32>, vel: vec2<f32>) -> vec2<f32> {
  // Pointer influence falls off with distance, not its square, so a press nudges instead of clearing.
  let toPointer = (frame.pointer.xy * 2.0 - 1.0) - pos;
  let pull = pointerInput(${P.hoverPull}, ${P.heldPush}) * 0.001 / max(dot(toPointer, toPointer), 0.018);
  let walls = sign(pos) * smoothstep(vec2<f32>(0.80), vec2<f32>(${P.edge}), abs(pos)) * 0.09;
  let field = circulation(pos) + toPointer * pull - walls - vel * 0.010;
  return limit(field * ${P.speed}, ${P.speedLimit});
}
@compute @workgroup_size(${ITEM_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  let i = id.x;
  if (i >= arrayLength(&particlesIn)) { return; }
  let p = particlesIn[i];
  let dt = frame.clock.x;
  // Midpoint advection keeps particles on the circulation instead of drifting outward.
  var vel = velocityAt(p.pos + velocityAt(p.pos, p.vel) * dt * 0.5, p.vel);
  var pos = p.pos + vel * dt;
  let outside = abs(pos) > vec2<f32>(${P.edge});
  pos = select(pos, sign(pos) * (2.0 * ${P.edge} - abs(pos)), outside);
  vel = select(vel, -sign(pos) * abs(vel) * ${P.bounce}, outside);
  particlesOut[i] = Particle(pos, vel, p.color);
}`;

const DRAW = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<uniform> look: Look;
@group(0) @binding(2) var<storage, read> particles: array<Particle>;
struct Out {
  @builtin(position) position: vec4<f32>,
  @location(0) local: vec2<f32>,
  @location(1) color: vec3<f32>,
  @location(2) energy: f32,
  @location(3) visibility: f32,
}
@vertex fn vs(@builtin(vertex_index) corner: u32, @builtin(instance_index) instance: u32) -> Out {
  let p = particles[instance];
  let local = vec2<f32>(f32(corner & 1u), f32(corner >> 1u)) * 2.0 - 1.0;
  let size = look.size * (0.75 + 0.5 * fract(f32(instance) * 0.618034));
  var out: Out;
  out.position = vec4<f32>((p.pos + local * size) / frame.view.xy, 0.0, 1.0);
  out.local = local;
  out.color = p.color.rgb * ${P.colorGain} * (0.85 + 0.15 * sin(frame.clock.y * 1.3 + f32(instance % 997u)));
  out.energy = clamp(length(p.vel) * 0.85, 0.0, 1.0);
  out.visibility = 1.0 - smoothstep(${P.fadeStart}, ${P.fadeEnd}, max(abs(p.pos.x), abs(p.pos.y)));
  return out;
}
@fragment fn fs(in: Out) -> @location(0) vec4<f32> {
  let d = length(in.local);
  if (d > 1.0 || in.visibility <= 0.0) { discard; }
  let core = 1.0 - smoothstep(0.0, 1.0, d);
  let halo = pow(1.0 - d, 3.0);
  let alpha = (core * 0.10 + halo * (0.14 + in.energy * 0.08)) * in.visibility;
  return vec4<f32>(in.color * 0.85 * alpha, alpha);
}`;

const TRAIL = /* wgsl */ `${SCREEN_VERTEX_WGSL}
@group(0) @binding(0) var history: texture_2d<f32>;
@fragment fn fs(@builtin(position) at: vec4<f32>) -> @location(0) vec4<f32> {
  let size = vec2<f32>(textureDimensions(history));
  let square = (at.xy / size * 2.0 - 1.0) * size / max(size.x, size.y);
  let vignette = 1.0 - smoothstep(0.15, 1.2, length(square));
  let background = vec3<f32>(${P.background.join(', ')}) + vignette * vec3<f32>(0.008, 0.020, 0.012);
  let edge = 1.0 - smoothstep(${P.fadeStart}, ${P.fadeEnd}, max(abs(square.x), abs(square.y)));
  let faded = textureLoad(history, vec2<i32>(at.xy), 0).rgb * ${P.trailRetention} * edge;
  return vec4<f32>(max(faded, background), 1.0);
}`;

// Fixed-point atomics make the splat independent of particle order.
const DEPOSIT = /* wgsl */ `${COMMON}
struct Deposit { weight: atomic<i32>, r: atomic<i32>, g: atomic<i32>, b: atomic<i32> }
@group(0) @binding(1) var<storage, read> particles: array<Particle>;
@group(0) @binding(2) var<storage, read_write> deposits: array<Deposit>;
@compute @workgroup_size(${ITEM_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (id.x >= arrayLength(&particles)) { return; }
  let p = particles[id.x];
  let spot = (p.pos * 0.5 + 0.5) * frame.grid.x - 0.5;
  let base = vec2<i32>(floor(spot));
  let f = spot - floor(spot);
  let color = clamp(p.color.rgb, vec3<f32>(0.0), vec3<f32>(4.0));
  for (var y = 0; y < 2; y++) {
    for (var x = 0; x < 2; x++) {
      let amount = select(1.0 - f.x, f.x, x == 1) * select(1.0 - f.y, f.y, y == 1) * ${P.fixedPoint}.0;
      let c = cell(base + vec2<i32>(x, y));
      atomicAdd(&deposits[c].weight, i32(amount));
      atomicAdd(&deposits[c].r, i32(color.r * amount));
      atomicAdd(&deposits[c].g, i32(color.g * amount));
      atomicAdd(&deposits[c].b, i32(color.b * amount));
    }
  }
}`;

const RESOLVE = /* wgsl */ `${COMMON}
struct Deposit { weight: i32, r: i32, g: i32, b: i32 }
@group(0) @binding(1) var<storage, read> deposits: array<Deposit>;
@group(0) @binding(2) var<storage, read_write> carryColor: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> carryMotion: array<vec4<f32>>;
@compute @workgroup_size(${CELL_WORKGROUP}, ${CELL_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (outsideGrid(id.xy)) { return; }
  let i = cell(vec2<i32>(id.xy));
  let d = deposits[i];
  let density = min(1.0, f32(d.weight) / ${P.fixedPoint}.0);
  carryColor[i] = vec4<f32>(vec3<f32>(f32(d.r), f32(d.g), f32(d.b)) / max(f32(d.weight), 1.0) * density, 1.0);
  // The stream field is dense even where particles are sparse; motion is carried in sheet uv units.
  carryMotion[i] = vec4<f32>(circulation(cellUv(id.xy) * 2.0 - 1.0) * ${P.speed} * 0.5, 0.0, 0.0);
}`;

const SEED = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<uniform> look: Look;
@group(0) @binding(2) var<storage, read> carryColor: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> carryMotion: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> particles: array<Particle>;
@compute @workgroup_size(${ITEM_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  let i = id.x;
  if (i >= arrayLength(&particles)) { return; }
  // A jittered lattice starts every particle where the incoming material already is, without moiré.
  let columns = u32(look.columns);
  let hash = (i * 1664525u + 1013904223u) ^ (i >> 9u);
  let jitter = vec2<f32>(f32(hash & 1023u), f32((hash >> 10u) & 1023u)) / 1023.0;
  let uv = (vec2<f32>(f32(i % columns), f32(i / columns)) + jitter) / vec2<f32>(look.columns, look.rows);
  let c = cell(vec2<i32>(uv * frame.grid.xy));
  let velocity = limit(carryMotion[c].xy * 2.0, ${P.importSpeedLimit});
  particles[i] = Particle(uv * 2.0 - 1.0, velocity, vec4<f32>(hue(carryColor[c].rgb, ${P.darkest}), 0.0));
}`;

function createParticles({ kit, grid, count, canvas, frame, carry, images }) {
  const particles = [0, 1].map((i) => kit.storage(`Particles ${i}`, count * P.strideBytes));
  const deposits = kit.storage('Particle deposits', grid * grid * 16);
  const look = kit.uniform('Particle look', 16);
  const columns = Math.ceil(Math.sqrt(count));
  const rows = Math.ceil(count / columns);
  const pipelines = {
    step: kit.compute('Particles motion', STEP),
    deposit: kit.compute('Particles deposit', DEPOSIT),
    resolve: kit.compute('Particles resolve', RESOLVE),
    seed: kit.compute('Particles seed', SEED),
    draw: kit.render('Particles glow', DRAW, { blend: P.blend }),
    trail: kit.render('Particles trail', TRAIL, { vertex: 'screenVertex' }),
  };
  const pair = (make) => [0, 1].map(make);
  const groups = {
    step: pair((i) => kit.bind(pipelines.step, [frame, particles[i], particles[1 - i]])),
    draw: pair((i) => kit.bind(pipelines.draw, [frame, look, particles[i]])),
    deposit: pair((i) => kit.bind(pipelines.deposit, [frame, particles[i], deposits])),
    resolve: carry.map((slot) => kit.bind(pipelines.resolve, [frame, deposits, slot.color, slot.motion])),
    seed: carry.map((slot) => kit.bind(pipelines.seed, [frame, look, slot.color, slot.motion, particles[0]])),
  };
  let current = 0, trail = 0, trails = [], trailGroups = [];
  function clearTrails(encoder) {
    for (const texture of trails) encoder.beginRenderPass({ colorAttachments: [kit.target(texture, P.background)] }).end();
  }
  return {
    background: P.background,
    parcelRadius: () => (P.radiusPx * 2) / Math.max(canvas.clientWidth, canvas.clientHeight, 1),
    particles: () => particles[current],
    resize() {
      const radius = Math.min(P.radiusRangePx[1],
        Math.max(P.radiusRangePx[0], P.radiusPx * (P.referenceCount / count) ** 0.25));
      kit.write(look, [(radius * 2) / Math.max(canvas.clientWidth, canvas.clientHeight, 1), columns, rows, 0]);
      kit.release(trails);
      trails = pair((i) => kit.texture(`Particle trail ${i}`, canvas.width, canvas.height));
      trailGroups = trails.map((texture) => kit.bind(pipelines.trail, [texture]));
      trail = 0;
    },
    simulate(pass, { steps }) {
      for (let step = 0; step < steps; step++) {
        kit.dispatchItems(pass, pipelines.step, groups.step[current], count);
        current = 1 - current;
      }
    },
    render(encoder, view) {
      const next = 1 - trail;
      const pass = encoder.beginRenderPass({ colorAttachments: [kit.target(trails[next], P.background)] });
      pass.setPipeline(pipelines.trail);
      pass.setBindGroup(0, trailGroups[trail]);
      pass.draw(4);
      pass.setPipeline(pipelines.draw);
      pass.setBindGroup(0, groups.draw[current]);
      pass.draw(4, count);
      pass.end();
      images.copy(encoder, view, trails[next]);
      trail = next;
    },
    exportCarry(encoder, slot) {
      encoder.clearBuffer(deposits);
      const pass = encoder.beginComputePass({ label: 'Particles export' });
      kit.dispatchItems(pass, pipelines.deposit, groups.deposit[current], count);
      kit.dispatchCells(pass, pipelines.resolve, groups.resolve[slot], grid);
      pass.end();
    },
    importCarry(encoder, slot) {
      const pass = encoder.beginComputePass({ label: 'Particles import' });
      kit.dispatchItems(pass, pipelines.seed, groups.seed[slot], count);
      pass.end();
      current = 0;
      clearTrails(encoder);
    },
  };
}

export { PARTICLE_POLICY, createParticles };
