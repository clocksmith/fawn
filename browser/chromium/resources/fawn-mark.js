// Fawn mark: the live scene gathers into the logo as moving material. A lattice of parcels
// covers the canvas; each carries the color under its origin and travels a curved path to its
// own place inside the mark, so the scene keeps moving while it forms the Fawn and reverses
// along the same paths when it lets go.
import { FRAME_WGSL, SCREEN_VERTEX_WGSL } from './fawn-gpu.js';

const MARK_POLICY = Object.freeze({
  spacingPx: 4,
  maxParcels: 262144,
  maskGrid: 384,
  launchSpread: 0.5,
  swirl: 0.3,
  stretch: 0.6,
  radiusScale: 1.0,
  coverageGain: 3,
  brightBias: 0.03,
  brandedLift: 0.8,
  handoff: Object.freeze([0, 0.06]),
  momentFormat: 'rgba16float',
  background: Object.freeze([0.008, 0.016, 0.02]),
});
const P = MARK_POLICY;

// lattice: columns, rows, canvas width, height (px); radius: splat radius (px), unused, handoff
// range; tone: background rgb, logo shape.
const LOOK_WGSL = /* wgsl */ `
struct Look { lattice: vec4<f32>, radius: vec4<f32>, tone: vec4<f32> }
`;

const SPLAT = /* wgsl */ `${FRAME_WGSL}${LOOK_WGSL}
@group(0) @binding(1) var<uniform> look: Look;
@group(0) @binding(2) var<storage, read> goals: array<vec2<f32>>;
@group(0) @binding(3) var scene: texture_2d<f32>;
struct Out {
  @builtin(position) position: vec4<f32>,
  @location(0) local: vec2<f32>,
  @location(1) color: vec3<f32>,
}
// Bright features such as film rims and particle points are thin; weighting by brightness keeps
// them from averaging away as the mark packs the image together.
fn brightWeight(c: vec3<f32>) -> f32 { return ${P.brightBias} + luminance(c); }
fn sceneColor(screen: vec2<f32>) -> vec3<f32> {
  let size = look.lattice.zw;
  let cellPx = size / look.lattice.xy;
  var sum = vec4<f32>(0.0);
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let at = vec2<f32>(screen.x, 1.0 - screen.y) * size + vec2<f32>(f32(x), f32(y)) * cellPx / 3.0;
      let c = textureLoad(scene, vec2<i32>(clamp(at, vec2<f32>(0.0), size - 1.0)), 0).rgb;
      sum += vec4<f32>(c, 1.0) * brightWeight(c);
    }
  }
  return sum.rgb / sum.a;
}
// Smooth regional noise in -1..1: neighboring material shares a launch time and a swirl.
fn regional(p: vec2<f32>) -> f32 {
  return 0.5 * sin(p.x * 3.7 + sin(p.y * 2.9)) + 0.5 * sin(p.y * 4.3 + sin(p.x * 3.1 + 1.7));
}
@vertex fn vs(@builtin(vertex_index) corner: u32, @builtin(instance_index) instance: u32) -> Out {
  let columns = u32(look.lattice.x);
  let screen = (vec2<f32>(f32(instance % columns), f32(instance / columns)) + 0.5) / look.lattice.xy;
  let origin = screen * 2.0 - 1.0;
  let goal = (goals[instance] * 2.0 - 1.0) / frame.view.xy;
  // Regions launch at different times and swirl their own way, so material streams into the mark
  // instead of moving as one sheet; every parcel still arrives when the shape completes.
  let delay = (0.5 + 0.5 * regional(origin)) * ${P.launchSpread};
  let t = smoothstep(0.0, 1.0, clamp((frame.view.w - delay) / (1.0 - delay), 0.0, 1.0));
  let flight = sin(3.14159265 * t);
  let pixels = look.lattice.zw * 0.5;
  let travel = (goal - origin) * pixels;
  let bend = vec2<f32>(-travel.y, travel.x) * flight * ${P.swirl} * regional(origin * 1.3 + 2.0) / pixels;
  // Splats stretch in flight so parting material pulls apart instead of cracking.
  let radius = look.radius.x * (1.0 + ${P.stretch} * flight);
  let local = vec2<f32>(f32(corner & 1u), f32(corner >> 1u)) * 2.0 - 1.0;
  var out: Out;
  out.position = vec4<f32>(mix(origin, goal, t) + bend + local * radius / pixels, 0.0, 1.0);
  out.local = local;
  out.color = sceneColor(screen);
  return out;
}
@fragment fn fs(in: Out) -> @location(0) vec4<f32> {
  let kernel = pow(max(0.0, 1.0 - dot(in.local, in.local)), 2.0) * brightWeight(in.color);
  return vec4<f32>(in.color * kernel, kernel);
}`;

const RESOLVE = /* wgsl */ `${SCREEN_VERTEX_WGSL}${LOOK_WGSL}
fn luminance(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722)); }
@group(0) @binding(0) var moments: texture_2d<f32>;
@group(0) @binding(1) var scene: texture_2d<f32>;
@group(0) @binding(2) var<uniform> look: Look;
@fragment fn fs(@builtin(position) at: vec4<f32>) -> @location(0) vec4<f32> {
  let m = textureLoad(moments, vec2<i32>(at.xy), 0);
  let live = textureLoad(scene, vec2<i32>(at.xy), 0).rgb;
  let moved = m.rgb / max(m.a, 1e-6);
  // Dark scenes still read as the mark: gathered material lifts its shadows as the shape completes.
  let lifted = mix(moved, sqrt(max(moved, vec3<f32>(0.0))), look.tone.w * ${P.brandedLift});
  let cover = 1.0 - exp(-m.a / (${P.brightBias} + luminance(moved)) * ${P.coverageGain});
  let gathered = mix(look.tone.rgb, lifted, cover);
  // The moving copy replaces the live image before its parcels have visibly left their origins.
  return vec4<f32>(mix(live, gathered, smoothstep(look.radius.z, look.radius.w, look.tone.w)), 1.0);
}`;

function invert(cdf, target) {
  let lo = 0, hi = cdf.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (cdf[mid] <= target) lo = mid;
    else hi = mid;
  }
  const span = cdf[lo + 1] - cdf[lo];
  return lo + (span > 0 ? (target - cdf[lo]) / span : 0.5);
}

// Separable monotone transport: canvas rows map to mark rows by cumulative area, then positions
// within a row by cumulative coverage. Neighbors stay neighbors and the mark fills evenly.
function markGoals(field, size, columns, rows) {
  const rowCdfs = [];
  const columnCdf = new Float64Array(size + 1);
  for (let y = 0; y < size; y++) {
    const cdf = new Float64Array(size + 1);
    for (let x = 0; x < size; x++) cdf[x + 1] = cdf[x] + field[(y * size + x) * 4 + 2];
    rowCdfs.push(cdf);
    columnCdf[y + 1] = columnCdf[y] + cdf[size];
  }
  const area = columnCdf[size];
  if (!area) throw new Error('Fawn mark requires visible logo geometry');
  const goals = new Float32Array(columns * rows * 2);
  for (let j = 0; j < rows; j++) {
    const y = invert(columnCdf, ((j + 0.5) / rows) * area);
    const row = rowCdfs[Math.min(size - 1, Math.floor(y))];
    for (let i = 0; i < columns; i++) {
      goals.set([invert(row, ((i + 0.5) / columns) * row[size]) / size, y / size], (j * columns + i) * 2);
    }
  }
  return goals;
}

function createMark({ kit, canvas, frame }) {
  const additive = { srcFactor: 'one', dstFactor: 'one', operation: 'add' };
  const pipelines = {
    splat: kit.render('Fawn mark parcels', SPLAT, { blend: { color: additive, alpha: additive }, target: P.momentFormat }),
    resolve: kit.render('Fawn mark', RESOLVE, { vertex: 'screenVertex' }),
  };
  const look = kit.uniform('Fawn mark look', 48);
  let goals = null, moments = null, columns = 0, rows = 0, radius = [0, 0], groups = null;
  return {
    resize(scene) {
      const width = Math.max(canvas.clientWidth, 1), height = Math.max(canvas.clientHeight, 1);
      const spacing = Math.max(P.spacingPx, Math.sqrt((width * height) / P.maxParcels));
      columns = Math.ceil(width / spacing);
      rows = Math.ceil(height / spacing);
      const field = FawnFormation.field(P.maskGrid, canvas, document.querySelector('.brand-logo'));
      const targets = markGoals(field, P.maskGrid, columns, rows);
      kit.release([goals, moments].filter(Boolean));
      goals = kit.storage('Fawn mark goals', targets.byteLength);
      kit.device.queue.writeBuffer(goals, 0, targets);
      moments = kit.texture('Fawn mark moments', canvas.width, canvas.height, P.momentFormat);
      // Rows keep most of their spacing inside the mark, so splats keep their size and overlap
      // where the transport packs material sideways.
      radius = [(canvas.width / columns) * P.radiusScale, 0];
      groups = {
        splat: kit.bind(pipelines.splat, [frame, look, goals, scene]),
        resolve: kit.bind(pipelines.resolve, [moments, scene, look]),
      };
    },
    draw(encoder, view, shape) {
      kit.write(look, [columns, rows, canvas.width, canvas.height, ...radius, ...P.handoff,
        ...P.background, shape]);
      const pass = encoder.beginRenderPass({ colorAttachments: [kit.target(moments, [0, 0, 0, 0])] });
      pass.setPipeline(pipelines.splat);
      pass.setBindGroup(0, groups.splat);
      pass.draw(4, columns * rows);
      pass.end();
      kit.drawScreen(encoder, view, pipelines.resolve, groups.resolve);
    },
  };
}

export { MARK_POLICY, createMark, markGoals };
