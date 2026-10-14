// Carrier: during a scene change, one live population moves the outgoing material through the
// blended motion of both scenes and takes on the incoming scene's color, size, and background.
import { FRAME_WGSL, ITEM_WORKGROUP, SCREEN_VERTEX_WGSL, fieldSampler } from './fawn-gpu.js';

const CARRIER_POLICY = Object.freeze({
  strideBytes: 32,
  speedLimit: 0.9,
  coverageGain: 2.5,
  exposure: 1.65,
  momentFormat: 'rgba16float',
  darkest: 0.004,
});
const C = CARRIER_POLICY;

// radius: outgoing and incoming splat radii, transition blend; lattice: seed columns and rows.
const LOOK_WGSL = /* wgsl */ `
struct Look { radius: vec4<f32>, fromBackground: vec4<f32>, toBackground: vec4<f32>, lattice: vec4<f32> }
`;
const COMMON = /* wgsl */ `${FRAME_WGSL}${LOOK_WGSL}
struct Parcel { pos: vec2<f32>, vel: vec2<f32>, color: vec4<f32> }
`;

const SEED = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<uniform> look: Look;
@group(0) @binding(2) var<storage, read> carryColor: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> carryMotion: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> parcels: array<Parcel>;
@compute @workgroup_size(${ITEM_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  let i = id.x;
  if (i >= arrayLength(&parcels)) { return; }
  let columns = u32(look.lattice.x);
  let uv = (vec2<f32>(f32(i % columns), f32(i / columns)) + 0.5) / look.lattice.xy;
  let c = cell(vec2<i32>(uv * frame.grid.xy));
  parcels[i] = Parcel(uv * 2.0 - 1.0, carryMotion[c].xy * 2.0, vec4<f32>(carryColor[c].rgb, 1.0));
}`;

const ADVECT = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<storage, read> parcelsIn: array<Parcel>;
@group(0) @binding(2) var<storage, read_write> parcelsOut: array<Parcel>;
@group(0) @binding(3) var<storage, read> fromMotion: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> toMotion: array<vec4<f32>>;
${fieldSampler('fromVelocity', 'fromMotion', 'vec2<f32>', '.xy')}
${fieldSampler('toVelocity', 'toMotion', 'vec2<f32>', '.xy')}
fn velocityAt(pos: vec2<f32>) -> vec2<f32> {
  let uv = pos * 0.5 + 0.5;
  // Carried motion is in sheet uv units; parcel positions span -1..1.
  return limit(mix(fromVelocity(uv), toVelocity(uv), frame.clock.w) * 2.0, ${C.speedLimit});
}
@compute @workgroup_size(${ITEM_WORKGROUP})
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
  let i = id.x;
  if (i >= arrayLength(&parcelsIn)) { return; }
  let p = parcelsIn[i];
  let dt = frame.clock.x * frame.clock.z;
  let velocity = velocityAt(p.pos + velocityAt(p.pos) * dt * 0.5);
  parcelsOut[i] = Parcel(clamp(p.pos + velocity * dt, vec2<f32>(-1.0), vec2<f32>(1.0)), velocity, p.color);
}`;

const SPLAT = /* wgsl */ `${COMMON}
@group(0) @binding(1) var<uniform> look: Look;
@group(0) @binding(2) var<storage, read> parcels: array<Parcel>;
@group(0) @binding(3) var<storage, read> toColor: array<vec4<f32>>;
${fieldSampler('incomingColor', 'toColor', 'vec3<f32>', '.rgb')}
struct Out {
  @builtin(position) position: vec4<f32>,
  @location(0) local: vec2<f32>,
  @location(1) color: vec3<f32>,
}
@vertex fn vs(@builtin(vertex_index) corner: u32, @builtin(instance_index) instance: u32) -> Out {
  let p = parcels[instance];
  let local = vec2<f32>(f32(corner & 1u), f32(corner >> 1u)) * 2.0 - 1.0;
  let blend = frame.clock.w;
  var out: Out;
  out.position = vec4<f32>((p.pos + local * mix(look.radius.x, look.radius.y, blend)) / frame.view.xy, 0.0, 1.0);
  out.local = local;
  // Each parcel keeps its own carried color and takes on the incoming material where it lands.
  out.color = mix(p.color.rgb, incomingColor(p.pos * 0.5 + 0.5), blend);
  return out;
}
struct Moments { @location(0) value: vec4<f32> }
@fragment fn fs(in: Out) -> Moments {
  let kernel = pow(max(0.0, 1.0 - dot(in.local, in.local)), 2.0);
  return Moments(vec4<f32>(in.color * kernel, kernel));
}`;

const RESOLVE = /* wgsl */ `${SCREEN_VERTEX_WGSL}${LOOK_WGSL}
@group(0) @binding(0) var moments: texture_2d<f32>;
@group(0) @binding(1) var<uniform> look: Look;
@fragment fn fs(@builtin(position) at: vec4<f32>) -> @location(0) vec4<f32> {
  let m = textureLoad(moments, vec2<i32>(at.xy), 0);
  let color = 1.0 - exp(-max(m.rgb / max(m.a, 1e-6), vec3<f32>(0.0)) * ${C.exposure});
  let background = mix(look.fromBackground.rgb, look.toBackground.rgb, look.radius.z);
  return vec4<f32>(mix(background, max(color, background), 1.0 - exp(-m.a * ${C.coverageGain})), 1.0);
}`;

function createCarrier({ kit, count, canvas, frame, carry }) {
  const parcels = [0, 1].map((i) => kit.storage(`Carrier parcels ${i}`, count * C.strideBytes));
  const look = kit.uniform('Carrier look', 64);
  const columns = Math.ceil(Math.sqrt(count));
  const lattice = [columns, Math.ceil(count / columns), 0, 0];
  const additive = { srcFactor: 'one', dstFactor: 'one', operation: 'add' };
  const pipelines = {
    seed: kit.compute('Carrier seed', SEED),
    advect: kit.compute('Carrier motion', ADVECT),
    splat: kit.render('Carrier splats', SPLAT, {
      blend: { color: additive, alpha: additive },
      target: C.momentFormat,
    }),
    resolve: kit.render('Carrier surface', RESOLVE, { vertex: 'screenVertex' }),
  };
  const pair = (make) => [0, 1].map(make);
  const groups = {
    seed: kit.bind(pipelines.seed, [frame, look, carry[0].color, carry[0].motion, parcels[0]]),
    advect: pair((i) => kit.bind(pipelines.advect,
      [frame, parcels[i], parcels[1 - i], carry[0].motion, carry[1].motion])),
    splat: pair((i) => kit.bind(pipelines.splat, [frame, look, parcels[i], carry[1].color])),
  };
  let current = 0, from = null, to = null, moments = null, resolveGroup = null;
  return {
    resize() {
      kit.release([moments].filter(Boolean));
      moments = kit.texture('Carrier moments', canvas.width, canvas.height, C.momentFormat);
      resolveGroup = kit.bind(pipelines.resolve, [moments, look]);
    },
    // Particles hand over their actual positions; fluids hand over a lattice of their cells.
    begin(encoder, outgoing, incoming) {
      from = outgoing;
      to = incoming;
      current = 0;
      if (outgoing.particles) {
        encoder.copyBufferToBuffer(outgoing.particles(), 0, parcels[0], 0, count * C.strideBytes);
        return;
      }
      const pass = encoder.beginComputePass({ label: 'Carrier seed' });
      kit.dispatchItems(pass, pipelines.seed, groups.seed, count);
      pass.end();
    },
    advect(pass) {
      kit.dispatchItems(pass, pipelines.advect, groups.advect[current], count);
      current = 1 - current;
    },
    render(encoder, view, blend) {
      // The seed pass in the same frame reads this write's lattice as well.
      kit.write(look, [from.parcelRadius(), to.parcelRadius(), blend, 0,
        ...from.background, 1, ...to.background, 1, ...lattice]);
      const pass = encoder.beginRenderPass({ colorAttachments: [kit.target(moments, [0, 0, 0, 0])] });
      pass.setPipeline(pipelines.splat);
      pass.setBindGroup(0, groups.splat[current]);
      pass.draw(4, count);
      pass.end();
      kit.drawScreen(encoder, view, pipelines.resolve, resolveGroup);
    },
  };
}

export { CARRIER_POLICY, createCarrier };
