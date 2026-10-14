// Fawn GPU kit: resource ownership, pipeline builders, and WGSL shared by every scene.
//
// Every scene shader binds the per-frame `Frame` uniform at binding 0. All state lives in one
// square "sheet" (uv 0..1, or -1..1 for point positions) cropped to the canvas aspect.
const FRAME_FLOATS = 24;
const CELL_WORKGROUP = 16;
const ITEM_WORKGROUP = 128;

const FRAME_WGSL = /* wgsl */ `
struct Frame {
  grid: vec4<f32>,    // cells, cells, 1 / cells, 1 / cells
  view: vec4<f32>,    // visible sheet extent x, y; canvas aspect; logo shape 0..1
  clock: vec4<f32>,   // seconds per step, scene seconds, steps per frame, transition blend
  pointer: vec4<f32>, // sheet uv x, y; travel since the previous frame x, y
  input: vec4<f32>,   // hover, held, press impulse, transition attenuation
  burst: vec4<f32>,   // magnet release: sheet uv x, y; ring radius; strength
}
@group(0) @binding(0) var<uniform> frame: Frame;
fn cell(p: vec2<i32>) -> u32 {
  let n = i32(frame.grid.x);
  let q = clamp(p, vec2<i32>(0), vec2<i32>(n - 1));
  return u32(q.y * n + q.x);
}
fn cellUv(id: vec2<u32>) -> vec2<f32> { return (vec2<f32>(id) + 0.5) * frame.grid.zw; }
fn outsideGrid(id: vec2<u32>) -> bool { return any(id >= vec2<u32>(frame.grid.xy)); }
fn safeNormalize(v: vec2<f32>) -> vec2<f32> { return v / max(length(v), 1e-5); }
fn limit(v: vec2<f32>, top: f32) -> vec2<f32> { return v / max(1.0, length(v) / top); }
fn luminance(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722)); }
fn hue(c: vec3<f32>, darkest: f32) -> vec3<f32> { return c / max(darkest, max(max(c.r, c.g), c.b)); }
fn spectral(t: f32) -> vec3<f32> { return 0.5 + 0.5 * cos(vec3<f32>(0.0, 2.0, 4.0) + t * 6.28318); }
fn tone(c: vec3<f32>, exposure: f32) -> vec3<f32> { return 1.0 - exp(-max(c, vec3<f32>(0.0)) * exposure); }
// Each scene supplies its own hover and held strengths; transitions attenuate both.
fn pointerInput(hover: f32, held: f32) -> f32 {
  return frame.input.x * mix(hover, held, frame.input.y) * frame.input.w;
}
`;

// WGSL cannot pass storage arrays to functions, so each field gets its own bilinear sampler.
function fieldSampler(name, buffer, type = 'vec4<f32>', swizzle = '') {
  const at = (offset) => `${buffer}[cell(b + vec2<i32>(${offset}))]${swizzle}`;
  return /* wgsl */ `
fn ${name}(uv: vec2<f32>) -> ${type} {
  let p = clamp(uv * frame.grid.xy - 0.5, vec2<f32>(0.0), frame.grid.xy - 1.0);
  let b = vec2<i32>(floor(p));
  let f = p - floor(p);
  return mix(mix(${at('0, 0')}, ${at('1, 0')}, f.x), mix(${at('0, 1')}, ${at('1, 1')}, f.x), f.y);
}`;
}

// The square sheet covers the canvas, cropping whichever axis is shorter.
const SHEET_WGSL = /* wgsl */ `
struct SheetOut {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
}
@vertex fn sheetVertex(@builtin(vertex_index) i: u32) -> SheetOut {
  let uv = vec2<f32>(f32(i & 1u), f32(i >> 1u));
  var out: SheetOut;
  out.position = vec4<f32>((uv * 2.0 - 1.0) / frame.view.xy, 0.0, 1.0);
  out.uv = uv;
  return out;
}`;

const SCREEN_VERTEX_WGSL = /* wgsl */ `
@vertex fn screenVertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
  return vec4<f32>(f32(i & 1u) * 2.0 - 1.0, f32(i >> 1u) * 2.0 - 1.0, 0.0, 1.0);
}`;

const IMAGE_WGSL = /* wgsl */ `${SCREEN_VERTEX_WGSL}
@group(0) @binding(0) var first: texture_2d<f32>;
@group(0) @binding(1) var second: texture_2d<f32>;
@group(0) @binding(2) var third: texture_2d<f32>;
@group(0) @binding(3) var<uniform> weights: vec4<f32>;
@fragment fn copy(@builtin(position) at: vec4<f32>) -> @location(0) vec4<f32> {
  return vec4<f32>(textureLoad(first, vec2<i32>(at.xy), 0).rgb, 1.0);
}
// Mixing in linear light keeps the midpoint between two materials from dimming.
@fragment fn compose(@builtin(position) at: vec4<f32>) -> @location(0) vec4<f32> {
  let a = textureLoad(first, vec2<i32>(at.xy), 0).rgb;
  let b = textureLoad(second, vec2<i32>(at.xy), 0).rgb;
  let c = textureLoad(third, vec2<i32>(at.xy), 0).rgb;
  return vec4<f32>(sqrt(a * a * weights.x + b * b * weights.y + c * c * weights.z), 1.0);
}`;

function createKit(device, format) {
  const owned = new Set();
  const own = (resource) => (owned.add(resource), resource);
  const copyable = GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;
  const kit = {
    device,
    format,
    storage: (label, bytes) =>
      own(device.createBuffer({ label, size: bytes, usage: copyable | GPUBufferUsage.STORAGE })),
    uniform: (label, bytes) =>
      own(device.createBuffer({ label, size: bytes, usage: copyable | GPUBufferUsage.UNIFORM })),
    texture: (label, width, height, textureFormat = format) =>
      own(device.createTexture({
        label,
        size: [width, height],
        format: textureFormat,
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
      })),
    write: (buffer, values) => device.queue.writeBuffer(buffer, 0, new Float32Array(values)),
    release(resources) {
      for (const resource of resources) {
        if (owned.delete(resource)) resource.destroy();
      }
    },
    compute(label, code) {
      const module = device.createShaderModule({ label, code });
      return device.createComputePipeline({ label, layout: 'auto', compute: { module, entryPoint: 'main' } });
    },
    render(label, code, { vertex = 'vs', fragment = 'fs', blend, target = format } = {}) {
      const module = device.createShaderModule({ label, code });
      return device.createRenderPipeline({
        label,
        layout: 'auto',
        vertex: { module, entryPoint: vertex },
        fragment: { module, entryPoint: fragment, targets: [{ format: target, ...(blend && { blend }) }] },
        primitive: { topology: 'triangle-strip' },
      });
    },
    // Bindings are positional: resources[i] binds to @binding(i).
    bind: (pipeline, resources) =>
      device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: resources.map((resource, binding) => ({
          binding,
          resource: resource instanceof GPUBuffer ? { buffer: resource }
            : resource instanceof GPUTexture ? resource.createView() : resource,
        })),
      }),
    dispatchCells(pass, pipeline, group, grid) {
      pass.setPipeline(pipeline);
      pass.setBindGroup(0, group);
      pass.dispatchWorkgroups(Math.ceil(grid / CELL_WORKGROUP), Math.ceil(grid / CELL_WORKGROUP));
    },
    dispatchItems(pass, pipeline, group, count) {
      pass.setPipeline(pipeline);
      pass.setBindGroup(0, group);
      pass.dispatchWorkgroups(Math.ceil(count / ITEM_WORKGROUP));
    },
    // Clear colors are opaque unless an alpha is given.
    target: (view, clear = [0, 0, 0]) =>
      ({ view: view instanceof GPUTexture ? view.createView() : view, loadOp: 'clear', storeOp: 'store',
        clearValue: [...clear, 1].slice(0, 4) }),
    drawScreen(encoder, view, pipeline, group, clear) {
      const pass = encoder.beginRenderPass({ colorAttachments: [kit.target(view, clear)] });
      pass.setPipeline(pipeline);
      pass.setBindGroup(0, group);
      pass.draw(4);
      pass.end();
    },
    destroy() {
      owned.forEach((resource) => resource.destroy());
      owned.clear();
    },
  };
  return kit;
}

function createImagePasses(kit) {
  const copy = kit.render('Fawn image copy', IMAGE_WGSL, { vertex: 'screenVertex', fragment: 'copy' });
  const compose = kit.render('Fawn transition compose', IMAGE_WGSL, {
    vertex: 'screenVertex',
    fragment: 'compose',
  });
  const weights = kit.uniform('Fawn transition weights', 16);
  // Bind groups follow texture lifetimes, which change only on resize.
  const groups = new WeakMap();
  const groupFor = (key, make) => groups.get(key) ?? groups.set(key, make()).get(key);
  return {
    copy(encoder, view, texture) {
      kit.drawScreen(encoder, view, copy, groupFor(texture, () => kit.bind(copy, [texture])));
    },
    compose(encoder, view, textures, [a, b, c]) {
      kit.write(weights, [a, b, c, 0]);
      const group = groupFor(textures[0], () => kit.bind(compose, [...textures, weights]));
      kit.drawScreen(encoder, view, compose, group);
    },
  };
}

export {
  CELL_WORKGROUP,
  FRAME_FLOATS,
  FRAME_WGSL,
  ITEM_WORKGROUP,
  SCREEN_VERTEX_WGSL,
  SHEET_WGSL,
  createImagePasses,
  createKit,
  fieldSampler,
};
