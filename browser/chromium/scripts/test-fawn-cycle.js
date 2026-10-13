// Real GPU handoff oracles and complete ordinary-Chrome cycle journeys.
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import http from 'node:http';
import path from 'node:path';
import { CYCLE_POLICY, createCycleClock } from '../resources/fawn-cycle-clock.js';
import { PARCEL_POLICY, createParcelData } from '../resources/fawn-parcels.js';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-cycle');
await mkdir(output, { recursive: true });
const files = [
  'fawn-cycle.html',
  'fawn-cycle.js',
  'fawn-cycle-clock.js',
  'fawn-cycle-gpu.js',
  'fawn-cycle-transfers.js',
  'fawn-parcels.js',
  'fawn-parcel-renderer.js',
  'fawn-shaders.js',
  'fawn-formation.js',
];
const report = {
  passed: false,
  classification:
    'Chrome WebGPU artwork and emulated screens; no native Doe, physical-phone or performance qualification',
  policy: CYCLE_POLICY,
  sources: [],
  oracles: [],
  journeys: [],
};
for (const file of files)
  report.sources.push({
    file,
    sha256: createHash('sha256')
      .update(await readFile(new URL(file, resources)))
      .digest('hex'),
  });
report.harnessSha256 = createHash('sha256')
  .update(await readFile(new URL(import.meta.url)))
  .digest('hex');
const clock = createCycleClock();
assert.equal(clock.advance(CYCLE_POLICY.holdS).started, true);
assert.equal(clock.advance(CYCLE_POLICY.blendS / 2).blend, 0.5);
const deferred = clock.advance(0.5, true);
assert.equal(deferred.phaseS, CYCLE_POLICY.holdS - CYCLE_POLICY.interactionQuietS);
assert.equal(deferred.blend, 0.5, 'Late input preserves the visible blend');
assert.equal(deferred.started, false, 'Late input must not repeat a GPU handoff');
assert.equal(clock.advance(CYCLE_POLICY.interactionQuietS).blend, 0.5);
assert.equal(clock.advance(CYCLE_POLICY.blendS).effect, 'wave');
assert.equal(clock.advance(0).blend, 0);
assert.throws(() => clock.advance(-1), /Invalid cycle timestep/);
clock.reset();
assert.equal(clock.state().effect, 'flow');
// Every square viewport fits inside the circular reservoir, including its corners.
const seedGrid = 64,
  seedCount = seedGrid ** 2;
const initialized = createParcelData(seedGrid, {
  dye: new Float32Array(seedCount * 4).fill(0.5),
  velocity: new Float32Array(seedCount * 4),
});
const reservoir = [];
for (let i = 0; i < seedCount; i++)
  reservoir.push(Math.hypot(initialized[i * 12], initialized[i * 12 + 1]));
assert.ok(Math.max(...reservoir) > Math.SQRT2);
assert.ok(Math.max(...reservoir) <= PARCEL_POLICY.initialRadius + 1e-6);
for (const x of [-0.95, 0, 0.95])
  for (const y of [-0.95, 0, 0.95]) {
    assert.ok(
      reservoir.some(
        (_, i) => Math.hypot(initialized[i * 12] - x, initialized[i * 12 + 1] - y) < 0.06,
      ),
      `No empty grid border near ${x},${y}`,
    );
  }
report.initialReservoir = { squareCornersCovered: true, circularBoundaryOutsideViewport: true };
const server = http.createServer(async (req, res) => {
  const file = new URL('.' + new URL(req.url, 'http://localhost').pathname, resources);
  if (!file.href.startsWith(resources.href)) {
    res.writeHead(403).end();
    return;
  }
  try {
    res.setHeader(
      'Content-Type',
      file.pathname.endsWith('.js')
        ? 'text/javascript'
        : file.pathname.endsWith('.svg')
          ? 'image/svg+xml'
          : 'text/html',
    );
    res.end(await readFile(file));
  } catch {
    res.writeHead(404).end();
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: ['--enable-unsafe-webgpu'],
});
report.browser = browser.version();
function instrument() {
  const p = (globalThis.cycleProbe = {
    clock: 1000,
    callbacks: new Map(),
    next: 0,
    device: null,
    buffers: new Map(),
    writes: [],
    dispatches: [],
    workgroups: [],
    errors: [],
    requests: 0,
    canvasContexts: 0,
  });
  Object.defineProperty(performance, 'now', { value: () => p.clock });
  requestAnimationFrame = (cb) => {
    p.callbacks.set(++p.next, cb);
    return p.next;
  };
  cancelAnimationFrame = (id) => p.callbacks.delete(id);
  const request = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function (...args) {
    p.requests++;
    p.adapter = {
      vendor: this.info.vendor,
      architecture: this.info.architecture,
      isFallbackAdapter: this.info.isFallbackAdapter,
    };
    const device = (p.device = await request.apply(this, args));
    device.addEventListener('uncapturederror', (e) => p.errors.push(e.error.message));
    const create = device.createBuffer.bind(device);
    device.createBuffer = (d) => {
      const b = create(d);
      if (d.label) p.buffers.set(d.label, b);
      return b;
    };
    const write = device.queue.writeBuffer.bind(device.queue);
    device.queue.writeBuffer = (b, o, data, ...rest) => {
      p.writes.push({
        label: b.label,
        uniform: Boolean(b.usage & GPUBufferUsage.UNIFORM),
        values: b.usage & GPUBufferUsage.UNIFORM ? Array.from(data) : undefined,
      });
      return write(b, o, data, ...rest);
    };
    const encode = device.createCommandEncoder.bind(device);
    device.createCommandEncoder = (...args) => {
      const e = encode(...args),
        begin = e.beginComputePass.bind(e);
      e.beginComputePass = (...args) => {
        const pass = begin(...args),
          pipeline = pass.setPipeline.bind(pass),
          dispatch = pass.dispatchWorkgroups.bind(pass);
        let label;
        pass.setPipeline = (pipelineObject) => {
          label = pipelineObject.label;
          pipeline(pipelineObject);
        };
        pass.dispatchWorkgroups = (...args) => {
          p.dispatches.push(label);
          p.workgroups.push({ label, groups: args });
          return dispatch(...args);
        };
        return pass;
      };
      return e;
    };
    return device;
  };
  const getContext = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, ...args) {
    if (kind === 'webgpu') p.canvasContexts++;
    return getContext.call(this, kind, ...args);
  };
}
async function advance(page, count, millis = 33) {
  await page.evaluate(
    async ({ count, millis }) => {
      for (let i = 0; i < count; i++) {
        cycleProbe.clock += millis;
        const callbacks = [...cycleProbe.callbacks.values()];
        cycleProbe.callbacks.clear();
        callbacks.forEach((cb) => cb(cycleProbe.clock));
        await cycleProbe.device.queue.onSubmittedWorkDone();
        await Promise.resolve();
      }
    },
    { count, millis },
  );
}
async function state(page) {
  return page.evaluate(async () => {
    const p = cycleProbe,
      fields = {};
    for (const [label, b] of p.buffers)
      if (b.usage & GPUBufferUsage.STORAGE) {
        const read = p.device.createBuffer({
          size: b.size,
          usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
        });
        const e = p.device.createCommandEncoder();
        e.copyBufferToBuffer(b, 0, read, 0, b.size);
        p.device.queue.submit([e.finish()]);
        await read.mapAsync(GPUMapMode.READ);
        const values = new Float32Array(read.getMappedRange()).slice();
        read.unmap();
        read.destroy();
        let central = 0;
        if (label.startsWith('Cycle parcels '))
          for (let i = 0; i < values.length; i += 12) {
            if (Math.abs(values[i]) < 0.25 && Math.abs(values[i + 1]) < 0.25) central++;
          }
        fields[label] = {
          ...(label.startsWith('Cycle parcels ') ? { central } : {}),
          finite: label === 'Cycle deposits' || values.every(Number.isFinite),
          energy: values.reduce((s, x) => s + Math.abs(x), 0),
        };
      }
    const weights = p.writes
      .filter((w) => w.label === 'Cycle parcel uniforms')
      .at(-1)
      ?.values?.slice(4, 7);
    const blend = weights?.find((w) => w > 0 && w < 1) || 0;
    return {
      effect: document.querySelector('#effect').textContent,
      fields,
      requests: p.requests,
      contexts: p.canvasContexts,
      allocations: p.buffers.size,
      uploads: p.writes.filter((w) => !w.uniform).length,
      handoffs: p.dispatches.filter((x) => x.startsWith('Cycle handoff')),
      errors: p.errors,
      blend,
      width: document.querySelector('#view').width,
      height: document.querySelector('#view').height,
    };
  });
}
try {
  const oracle = await browser.newPage();
  await oracle.goto(origin + '/fawn-icon.svg');
  report.oracles = await oracle.evaluate(async () => {
    const codes = await import('./fawn-cycle-transfers.js');
    const adapter = await navigator.gpu.requestAdapter(),
      device = await adapter.requestDevice();
    const errors = [];
    device.addEventListener('uncapturederror', (e) => errors.push(e.error.message));
    const grid = 4,
      count = 16;
    const buffer = (size, uniform = false) =>
      device.createBuffer({
        size,
        usage:
          GPUBufferUsage.COPY_DST |
          GPUBufferUsage.COPY_SRC |
          (uniform ? GPUBufferUsage.UNIFORM : GPUBufferUsage.STORAGE),
      });
    const uniform = buffer(48, true);
    device.queue.writeBuffer(
      uniform,
      0,
      new Float32Array([grid, grid, 1 / grid, 1 / grid, 1, 1, 0, 0, 0, 0, 1, 1]),
    );
    const dye = buffer(count * 16),
      velocity = buffer(count * 16),
      surface = buffer(count * 16),
      points = buffer(count * 48),
      deposit = buffer(count * 24);
    const d = new Float32Array(count * 4),
      v = new Float32Array(count * 4);
    for (let i = 0; i < count; i++) {
      d.set([i / 20, 0.2, 0.4, 1], i * 4);
      v.set([0.1, -0.2, 0, 0], i * 4);
    }
    device.queue.writeBuffer(dye, 0, d);
    device.queue.writeBuffer(velocity, 0, v);
    const run = (code, entries) => {
      const pipeline = device.createComputePipeline({
        layout: 'auto',
        compute: { module: device.createShaderModule({ code }), entryPoint: 'main' },
      });
      const group = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: entries.map((r, i) => ({
          binding: i,
          resource: r instanceof GPUBuffer ? { buffer: r } : r,
        })),
      });
      const e = device.createCommandEncoder(),
        pass = e.beginComputePass();
      pass.setPipeline(pipeline);
      pass.setBindGroup(0, group);
      pass.dispatchWorkgroups(1);
      pass.end();
      device.queue.submit([e.finish()]);
    };
    const read = async (b) => {
      const r = device.createBuffer({
        size: b.size,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
      });
      const e = device.createCommandEncoder();
      e.copyBufferToBuffer(b, 0, r, 0, b.size);
      device.queue.submit([e.finish()]);
      await r.mapAsync(GPUMapMode.READ);
      const values = new Float32Array(r.getMappedRange()).slice();
      r.unmap();
      r.destroy();
      return values;
    };
    run(codes.FLOW_TO_WAVE, [uniform, dye, velocity, surface]);
    const s = await read(surface);
    let error = 0;
    for (let i = 0; i < count; i++) {
      const light = d[i * 4] * 0.2126 + 0.2 * 0.7152 + 0.4 * 0.0722;
      error = Math.max(
        error,
        Math.abs(s[i * 4] - light * 0.65),
        Math.abs(s[i * 4 + 1] - Math.hypot(0.1, -0.2) * 0.08),
        Math.abs(s[i * 4 + 3] - light * 12),
      );
    }
    if (error > 1e-5) throw new Error(`Flow-to-Wave reference mismatch: ${error}`);
    const p = new Float32Array(count * 12);
    for (let i = 0; i < count; i++)
      p.set(
        [
          (((i % grid) + 0.5) / grid) * 2 - 1,
          ((Math.floor(i / grid) + 0.5) / grid) * 2 - 1,
          0.1,
          -0.15,
          0.8,
          0.3,
          0.2,
          1,
          0.7,
          0.2,
          0.1,
          0.04,
        ],
        i * 12,
      );
    device.queue.writeBuffer(points, 0, p);
    // Centered source cells deposit their actual pigment and half their source velocity back to Flow.
    run(codes.PARTICLE_DEPOSIT, [uniform, points, deposit]);
    run(codes.RESOLVE_DEPOSIT, [uniform, deposit, dye, velocity]);
    const resolved = await read(dye),
      resolvedV = await read(velocity);
    let depositError = 0;
    for (let i = 0; i < count; i++) {
      for (let c = 0; c < 3; c++)
        depositError = Math.max(depositError, Math.abs(resolved[i * 4 + c] - p[i * 12 + 4 + c]));
      for (let c = 0; c < 2; c++)
        depositError = Math.max(
          depositError,
          Math.abs(resolvedV[i * 4 + c] - p[i * 12 + 2 + c] * 0.5),
        );
    }
    if (depositError > 1 / codes.TRANSFER_POLICY.fixedScale)
      throw new Error(`Particle deposit reference mismatch: ${depositError}`);
    await device.queue.onSubmittedWorkDone();
    if (errors.length) throw new Error(errors.join('\n'));
    const { PARCEL_UPDATE, PARCEL_POLICY } = await import('./fawn-parcels.js');
    const parcelUniform = buffer(64, true),
      moved = buffer(count * 48),
      target = buffer(count * 16),
      curl = buffer(count * 16);
    const goals = new Float32Array(count * 4);
    for (let i = 0; i < count; i++) goals.set([0.15 + i * 0.006, -0.12 + i * 0.008, 1, 0], i * 4);
    device.queue.writeBuffer(target, 0, goals);
    const dt = 0.01,
      time = 0.8,
      input = p.slice(),
      pose = [0.15, 0.2],
      motion = [0.04, -0.02];
    for (let i = 0; i < count; i++)
      input.set([...pose, ...motion, 1, 0.5, 0.25, 1, 0.7, 0.2, 0.1, 0.04], i * 12);
    device.queue.writeBuffer(points, 0, input);
    const parcelRun = (weights, shape) => {
      device.queue.writeBuffer(
        parcelUniform,
        0,
        new Float32Array([grid, count, dt, time, ...weights, shape, 0, 0, 0, dt, 1, 1, 0, 0]),
      );
      run(PARCEL_UPDATE, [parcelUniform, points, moved, target, velocity, dye, surface, curl]);
    };
    parcelRun([0, 0, 1], 0);
    const advanced = await read(moved);
    // Independently differentiate the stream function; no copied analytic WGSL derivative.
    const stream = ([x, y]) =>
      Math.cos((x * Math.PI) / 2) *
      Math.cos((y * Math.PI) / 2) *
      (1 + 0.35 * Math.sin(x * 2 + time * 0.2) * Math.sin(y * 2 - time * 0.16)) *
      0.045;
    const velocityAt = ([x, y]) => {
      const h = 1e-5;
      return [
        ((stream([x, y + h]) - stream([x, y - h])) / (2 * h)) * PARCEL_POLICY.fieldSpeed,
        (-(stream([x + h, y]) - stream([x - h, y])) / (2 * h)) * PARCEL_POLICY.fieldSpeed,
      ];
    };
    const startVelocity = velocityAt(pose),
      scale = Math.max(1, Math.hypot(...startVelocity) / PARCEL_POLICY.speedLimit);
    const midpoint = pose.map((p, i) => p + ((startVelocity[i] / scale) * dt) / 2),
      nextVelocity = velocityAt(midpoint);
    const nextScale = Math.max(1, Math.hypot(...nextVelocity) / PARCEL_POLICY.speedLimit);
    const expected = pose.map((p, i) => p + (nextVelocity[i] / nextScale) * dt);
    const advectionError = Math.max(...expected.map((p, i) => Math.abs(advanced[i] - p)));
    if (advectionError > 1e-5) throw new Error(`Midpoint advection mismatch: ${advectionError}`);
    const formation = [];
    for (const [name, weights] of [
      ['flow', [1, 0, 0]],
      ['wave', [0, 1, 0]],
      ['particles', [0, 0, 1]],
    ]) {
      // Irregular live positions must settle from their own values, never be respawned on a grid.
      for (let i = 0; i < count; i++) input.set([-0.5 + i * 0.04, 0.42 - i * 0.027], i * 12);
      device.queue.writeBuffer(points, 0, input);
      parcelRun(weights, 1);
      const settled = await read(moved);
      let formationError = 0,
        colorError = 0;
      const distances = [];
      for (let i = 0; i < count; i++) {
        const hash = ((Math.imul(i, 1664525) + 1013904223) ^ (i >>> 9)) >>> 0;
        const arrival = 0.35 + (1.65 * ((hash >>> 16) & 65535)) / 65535;
        const factor = 1 - Math.exp(-PARCEL_POLICY.settleRate * dt * arrival);
        for (let c = 0; c < 2; c++)
          formationError = Math.max(
            formationError,
            Math.abs(
              settled[i * 12 + c] -
                (input[i * 12 + c] + (goals[(hash % count) * 4 + c] - input[i * 12 + c]) * factor),
            ),
          );
        for (let c = 4; c < 12; c++)
          colorError = Math.max(colorError, Math.abs(settled[i * 12 + c] - input[i * 12 + c]));
        distances.push(
          Math.hypot(settled[i * 12] - input[i * 12], settled[i * 12 + 1] - input[i * 12 + 1]),
        );
      }
      if (formationError > 1e-6 || colorError > 1e-6)
        throw new Error(`${name} per-parcel formation mismatch: ${formationError}, ${colorError}`);
      formation.push({
        name,
        formationError,
        carriedMaterialError: colorError,
        maxStep: Math.max(...distances),
      });
    }
    await device.queue.onSubmittedWorkDone();
    if (errors.length) throw new Error(errors.join('\n'));
    device.destroy();
    return [
      { boundary: 'Parcel midpoint advection', maxError: advectionError },
      { boundary: 'All three per-parcel logo formations', formation },
      { boundary: 'Flow → Wave', maxError: error },
      { boundary: 'Particles → Flow', maxError: depositError },
    ];
  });
  await oracle.close();
  for (const viewport of [
    { width: 1200, height: 800 },
    { width: 390, height: 844 },
  ]) {
    const page = await browser.newPage({ viewport, deviceScaleFactor: 3 });
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));
    await page.addInitScript(instrument);
    await page.goto(origin + '/fawn-cycle.html');
    await page.waitForFunction(
      () => document.querySelector('#status').textContent === 'WebGPU active',
      null,
      { polling: 100 },
    );
    assert.equal(await page.locator('.demo-switcher').count(), 0);
    const defaultCount = viewport.width < 600 ? 16384 : 65536;
    assert.equal(await page.locator('#count-select').inputValue(), String(defaultCount));
    assert.equal(await page.locator('#steps-select').inputValue(), '4');
    assert.equal(
      await page.evaluate(() => cycleProbe.buffers.get('Cycle parcels 0').size),
      defaultCount * 48,
    );
    assert.equal(
      await page.evaluate(() => cycleProbe.buffers.get('Cycle dye 0').size),
      defaultCount * 16,
    );

    await advance(page, 100);
    const initial = await state(page);
    const projection = await page.evaluate(() => {
      const writes = cycleProbe.writes.filter((w) => w.label === 'Cycle parcel rendering uniforms');
      const poses = cycleProbe.writes
        .filter((w) => w.label === 'Cycle parcel uniforms')
        .map((w) => w.values[7]);
      const origin = writes[0].values.slice(0, 3);
      return {
        fixed: writes.every((w) => w.values.slice(0, 3).every((n, i) => n === origin[i])),
        formationAdvanced: Math.max(...poses) > 0.1,
      };
    });
    assert.equal(
      projection.fixed,
      true,
      'Logo formation must never zoom the viewport or splat radius',
    );
    assert.equal(projection.formationAdvanced, true);
    const timeline = [];
    for (let i = 0; i < 4; i++) {
      await advance(
        page,
        Math.ceil((CYCLE_POLICY.holdS + CYCLE_POLICY.blendS) / CYCLE_POLICY.maxDtS),
      );
      timeline.push(await state(page));
    }
    assert.deepEqual(
      timeline.map((s) => s.effect),
      ['Magnets', 'Particles', 'Flows', 'Magnets'],
    );
    for (const s of timeline) {
      assert.deepEqual(s.errors, []);
      assert.equal(s.requests, 1);
      assert.equal(s.contexts, 1);
      assert.equal(s.uploads, initial.uploads);
      assert.equal(s.allocations, initial.allocations);
      assert.ok(Object.values(s.fields).every((f) => f.finite));
    }
    if (viewport.width >= 600) {
      const dispatches = await page.evaluate(() => cycleProbe.workgroups);
      assert.ok(
        dispatches.some(
          (d) => d.label === 'Cycle handoff 0' && d.groups[0] === Math.ceil(defaultCount / 128),
        ),
        'Flow-to-film conversion must include the tail cells beyond the parcel count',
      );
      assert.ok(
        dispatches.some(
          (d) => d.label === 'Cycle handoff 2' && d.groups[0] === Math.ceil(defaultCount / 128),
        ),
        'Deposit resolution must include all fluid cells',
      );
    }
    assert.ok(timeline[1].fields['Cycle parcels 0'].energy > 0);
    assert.ok(
      timeline[1].fields['Cycle parcels 0'].central > 0,
      'Incoming particles retain material in the center',
    );
    assert.ok(timeline[2].fields['Cycle dye 0'].energy > 0);
    // Held input releases the logo and caps late scene time without restarting it.
    await advance(
      page,
      Math.floor(
        (CYCLE_POLICY.holdS - 100 * CYCLE_POLICY.maxDtS + CYCLE_POLICY.blendS / 2) /
          CYCLE_POLICY.maxDtS,
      ),
    );
    const before = await state(page);
    await page.mouse.move(viewport.width / 2, viewport.height / 2);
    await page.mouse.down();
    await advance(page, 120);
    const held = await state(page);
    assert.equal(held.effect, before.effect, 'Held input defers the scene transition');
    assert.ok(
      (await page.evaluate(
        () => cycleProbe.writes.filter((w) => w.label === 'Cycle parcel uniforms').at(-1).values[7],
      )) < 0.1,
      'Pressing releases the branded formation',
    );
    await page.mouse.up();
    await advance(page, 200 + Math.ceil(
      (CYCLE_POLICY.interactionQuietS + CYCLE_POLICY.blendS) / CYCLE_POLICY.maxDtS,
    ));
    assert.ok(
      (await page.evaluate(
        () => cycleProbe.writes.filter((w) => w.label === 'Cycle parcel uniforms').at(-1).values[7],
      )) > 0.75,
      'Idle input returns to branded Fawn',
    );
    await page.locator('#pause').click();
    const paused = await state(page);
    await advance(page, 120);
    const still = await state(page);
    assert.deepEqual(still.fields, paused.fields);
    assert.equal(still.effect, paused.effect);
    assert.equal(still.blend, paused.blend);
    await page.setViewportSize({ width: viewport.height, height: viewport.width });
    await page.waitForTimeout(80);
    await advance(page, 2);
    const rotated = await state(page);
    assert.equal(rotated.width, viewport.height * 2);
    assert.equal(rotated.height, viewport.width * 2);
    assert.equal(
      await page.evaluate(() => document.documentElement.scrollWidth > innerWidth),
      false,
    );
    await page.locator('#restart').click();
    await advance(page, 1);
    assert.equal((await state(page)).effect, 'Flows');
    assert.equal(await page.locator('#pause').textContent(), 'Resume');
    await page.locator('#pause').click();
    await advance(page, 2);
    await page.evaluate(() => {
      Object.defineProperty(document, 'hidden', { configurable: true, value: true });
      document.dispatchEvent(new Event('visibilitychange'));
    });
    const hidden = await state(page);
    await advance(page, 500);
    assert.deepEqual((await state(page)).fields, hidden.fields);
    await page.evaluate(() => {
      Object.defineProperty(document, 'hidden', { configurable: true, value: false });
      document.dispatchEvent(new Event('visibilitychange'));
    });
    await advance(page, 1);
    assert.equal((await state(page)).effect, 'Flows');
    assert.deepEqual(errors, []);
    report.journeys.push({
      viewport,
      adapter: await page.evaluate(() => cycleProbe.adapter),
      timeline: timeline.map((s) => ({ effect: s.effect, handoffs: s.handoffs, fields: s.fields })),
      fixedProjectionDuringFormation: projection,
      uploads: initial.uploads,
      allocations: initial.allocations,
      interactionContinued: true,
      pauseFrozen: true,
      hiddenFrozen: true,
      rotation: true,
      reset: true,
    });
    await page.screenshot({ path: path.join(output, `preview-${viewport.width}.png`) });
    await page.close();
    console.log(
      `Cycle, input, pause, visibility, reset, and rotation: ${viewport.width} × ${viewport.height}`,
    );
  }
  report.lifecycle = [];
  for (const reducedMotion of ['no-preference', 'reduce']) {
    const page = await browser.newPage({
      viewport: { width: 300, height: 150 },
      deviceScaleFactor: 1,
      reducedMotion,
    });
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));
    await page.addInitScript(instrument);
    await page.goto(origin + '/fawn-cycle.html');
    await page.waitForFunction(
      () => /WebGPU active|Paused/.test(document.querySelector('#status').textContent),
      null,
      { polling: 100 },
    );
    await advance(page, 2);
    const initial = await state(page);
    assert.equal(initial.width, 300);
    assert.equal(initial.height, 150);
    assert.deepEqual(initial.errors, []);
    if (reducedMotion === 'reduce') {
      assert.equal(await page.locator('#pause').textContent(), 'Resume');
      await advance(page, 500);
      assert.deepEqual((await state(page)).fields, initial.fields);
    } else {
      await page.evaluate(() =>
        Object.defineProperty(window, 'devicePixelRatio', { configurable: true, value: 2 }),
      );
      await advance(page, 2);
      assert.equal((await state(page)).width, 600);
      assert.equal((await state(page)).height, 300);
      await advance(page, Math.ceil((CYCLE_POLICY.holdS + CYCLE_POLICY.blendS) / 0.04) + 1, 40);
      assert.equal(
        (await state(page)).effect,
        'Magnets',
        '25 fps must not stretch the scene schedule',
      );
    }
    await page.evaluate(() =>
      cycleProbe.device.dispatchEvent(
        new GPUUncapturedErrorEvent('uncapturederror', {
          error: new GPUValidationError('Cycle fixture terminal error'),
        }),
      ),
    );
    assert.equal(await page.locator('#status').textContent(), 'Unavailable');
    assert.equal(await page.locator('#failure').textContent(), 'Cycle fixture terminal error');
    assert.equal(await page.locator('#pause').isDisabled(), true);
    await advance(page, 10);
    assert.equal(await page.evaluate(() => cycleProbe.callbacks.size), 0);
    assert.deepEqual(errors, []);
    report.lifecycle.push({
      reducedMotion,
      initialCanvasConfigured: true,
      dprOnly: reducedMotion === 'no-preference',
      elapsedSceneTimeAt25fps: reducedMotion === 'no-preference',
      terminalError: true,
    });
    await page.close();
  }
  report.passed = true;
} catch (error) {
  report.error = error.stack;
  throw error;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
