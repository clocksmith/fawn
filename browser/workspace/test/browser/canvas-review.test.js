// A canvas supplies the only status signal. The site's revision-conditional form
// and its durable review record establish which job can be queued and recovered.
import assert from 'node:assert/strict';
import { appendFileSync, existsSync, mkdtempSync, readFileSync, realpathSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { summarizeResidentOutcome } from '../../qualification/summaries.js';
import { WorkspaceService } from '../../src/service.js';
import { createPlaywrightAdapter } from '../../src/adapters/playwright-adapter.js';
import { testBrowserSelection } from '../support/browser-selection.js';
import { assertSolidPixels } from '../support/gpu-presentation-oracle.js';

const canvasBackend = process.env.BLITZEN_TEST_CANVAS_BACKEND ?? 'cpu';
const compositor = process.env.BLITZEN_REVIEW_COMPOSITOR_TRANSPORT;
const sandboxedSoftware = process.env.BLITZEN_RESIDENT_SANDBOXED_SOFTWARE === '1';
const frameTracePrefix = process.env.BLITZEN_RESIDENT_FRAME_TRACE_PREFIX;
const expectPlaneEvidence = process.env.BLITZEN_TEST_FAWN_PLANE_EVIDENCE === '1';
const observerMode = process.env.BLITZEN_RESIDENT_OBSERVER;
const stockTracePrefix = process.env.BLITZEN_RESIDENT_STOCK_TRACE_PREFIX;
const traceCategory = 'disabled-by-default-gpu.dawn';
const frameEventNames = new Set([
  'FawnResidentTrackRestriction', 'FawnResidentBrowserRestriction', 'FawnResidentFrameImported', 'FawnResidentStreamClosed',
  'FawnResidentResourceEdge', 'FawnResidentEncodedSubmit', 'FawnResidentDecodedSubmit',
  'FawnResidentDocumentValidated', 'FawnResidentDocumentRetired', 'FawnResidentCommandTerminal',
]);
const useCompositor = typeof compositor === 'string' && compositor.length > 0;
if (useCompositor && process.env.BLITZEN_TEST_ELEMENT_CAPTURE !== '1') throw Error('Visible, authorized compositor capture required');
// Private, correctness-only diagnostic. The browser supplies the trace and
// loss indicator over CDP. The site cannot write the controller's output.
async function startScopeTrace(page, prefix, names = null) {
  const session = await page.context().newCDPSession(page);
  const events = [];
  let overflow = false;
  session.on('Tracing.dataCollected', ({ value }) => {
    for (const event of value) {
      if (event.cat?.split(',').includes(traceCategory) &&
          event.name?.startsWith('FawnResident') && (!names || names.has(event.name))) {
        if (events.length < 20000) events.push(event);
        else overflow = true;
      }
    }
  });
  try { await session.send('Tracing.start', { categories: traceCategory, transferMode: 'ReportEvents' }); }
  catch (error) { await session.detach(); throw error; }
  return async request => {
    let timer;
    try {
      const complete = new Promise((resolve, reject) => {
        session.once('Tracing.tracingComplete', resolve);
        timer = setTimeout(() => reject(Error('Browser trace did not complete')), 10000);
      });
      const [, params] = await Promise.all([session.send('Tracing.end'), complete]);
      if (overflow || params.dataLossOccurred !== false) throw Error('Incomplete browser diagnostic trace');
      for (const [suffix, value] of [['trace', { traceEvents: events }], ['complete', params], ['scope', request]]) {
        writeFileSync(prefix + '.' + suffix + '.json', JSON.stringify(value) + '\n', { flag: 'wx', mode: 0o600 });
      }
    } finally { clearTimeout(timer); await session.detach(); }
  };
}

// Load Doe in another document, so the measured document still requests its
// own fresh decoder while the browser trace is active. The GPU child identity
// comes from this explicitly launched browser's private CDP pipe.
async function awaitNativeObserver(page, directory, pinnedLibrary) {
  const control = await page.context().browser().newBrowserCDPSession();
  const warm = await page.context().newPage();
  try {
    await warm.goto(origin + '/warm-attribution');
    await warm.evaluate(async () => {
      const adapter = await navigator.gpu?.requestAdapter();
      if (!adapter) throw Error('Private diagnostic GPU adapter unavailable');
      const device = await adapter.requestDevice();
      try { await device.queue.onSubmittedWorkDone(); } finally { device.destroy(); }
    });
    const { processInfo } = await control.send('SystemInfo.getProcessInfo');
    const { gpu } = await control.send('SystemInfo.getInfo');
    const { arguments: flags } = await control.send('Browser.getBrowserCommandLine');
    if (gpu.auxAttributes?.sandboxed !== true || flags.some(f =>
      /^--(?:no-sandbox|disable-(?:gpu|setuid|namespace|seccomp-filter)-sandbox|single-process|in-process-gpu)(?:=|$)/.test(f)) ||
      !flags.includes('--fawn-resident-command-provenance') || !flags.includes('--use-webgpu-runtime=doe') ||
      !flags.includes('--doe-webgpu-library-path=' + realpathSync.native(pinnedLibrary))) throw Error('Browser reported an incompatible sandbox or runtime');
    const unique = role => {
      const ids = processInfo.filter(p => p.type.toLowerCase() === role).map(p => p.id);
      if (ids.length !== 1 || !Number.isSafeInteger(ids[0]) || ids[0] <= 0) throw Error('Expected one browser-owned ' + role + ' child');
      return ids[0];
    };
    const ready = { run: process.env.BLITZEN_NATIVE_TRACE_RUN_ID, controllerPid: process.pid,
      browserPid: unique('browser'), gpuPid: unique('gpu'), gpuSandboxed: true };
    writeFileSync(join(directory, 'ready.partial'), JSON.stringify(ready), { flag: 'wx', mode: 0o600 });
    renameSync(join(directory, 'ready.partial'), join(directory, 'ready.json'));
    const deadline = Date.now() + Number(process.env.BLITZEN_CAPTURE_TIMEOUT_MS);
    const armedPath = join(directory, 'armed.json');
    while (!existsSync(armedPath)) {
      if (Date.now() >= deadline) throw Error('Native observer did not arm in the allotted window');
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    const armed = JSON.parse(readFileSync(armedPath, 'utf8'));
    if (armed.run !== ready.run || armed.gpuPid !== ready.gpuPid) throw Error('Native handoff belongs to another process');
  } finally { await warm.close(); await control.detach(); }
}

// Fault control on the private fixture. A second ordinary page gets its own
// browser chooser, restricts to the same element and copies the command label.
async function exerciseLookalike(page, copiedLabel) {
  const cdp = await page.context().newCDPSession(page);
  try {
    const controls = await cdp.send('Runtime.evaluate', { userGesture: true, awaitPromise: true,
      returnByValue: true, expression: `(async () => {
        const element = document.querySelector('#cell');
        let stream, secondDisplayCapture, device;
        try {
          try {
            stream = await navigator.mediaDevices.getDisplayMedia({ video: {displaySurface: 'browser'}, audio: false,
              preferCurrentTab: true, surfaceSwitching: 'exclude' });
            await stream.getVideoTracks()[0].restrictTo(await RestrictionTarget.fromElement(element));
            secondDisplayCapture = 'restricted';
          } catch (error) {
            if (error.name !== 'NotReadableError' && error.name !== 'NotAllowedError') throw error;
            stream?.getTracks().forEach(track => track.stop());
            secondDisplayCapture = 'refused-' + error.name;
            stream = element.captureStream(5);
          }
          const otherTrack = stream.getVideoTracks()[0];
          if (otherTrack.readyState !== 'live') throw Error('Second track did not open');
          const adapter = await navigator.gpu.requestAdapter();
          device = await adapter.requestDevice();
          const module = device.createShaderModule({code: '@compute @workgroup_size(1) fn main() {}'});
          const pipe = await device.createComputePipelineAsync({layout:'auto', compute:{module, entryPoint:'main'}});
          const encoder = device.createCommandEncoder(), pass = encoder.beginComputePass();
          pass.setPipeline(pipe); pass.dispatchWorkgroups(1); pass.end();
          device.queue.submit([encoder.finish({label:${JSON.stringify(copiedLabel)}})]);
          await device.queue.onSubmittedWorkDone();
          return { secondDisplayCapture, otherTrackLive:true, copiedLabelSubmitted:true,
            privateCaptureVisible:!!globalThis.__blitzenSurface };
        } finally { device?.destroy(); stream?.getTracks().forEach(track => track.stop()); }
      })()` });
    if (controls.exceptionDetails) throw Error(controls.exceptionDetails.exception?.description ?? controls.exceptionDetails.text);
    const actual = controls.result.value;
    assert.ok(['restricted', 'refused-NotReadableError', 'refused-NotAllowedError'].includes(actual.secondDisplayCapture));
    assert.ok(actual.otherTrackLive && actual.copiedLabelSubmitted && !actual.privateCaptureVisible);
    return controls.result.value;
  } finally { await cdp.detach(); }
}

const captures = [];
let latestFixtureBitmap, latestCapture;
function captureReceipt(frame, bitmap, metrics) {
  assert.equal(frame.pixelRepresentation, 'video-frame-copy-to-srgb-rgba8');
  assert.equal(bitmap.format, 'compositor-element-canonical-srgb-rgba8-v2');
  return { sessionId: frame.sessionId, grantId: frame.grantId, grantRevision: frame.grantRevision,
    mediaTimestampUs: frame.mediaTimestampUs, sourcePixelFormat: frame.sourcePixelFormat,
    width: bitmap.width, height: bitmap.height, tileSize: bitmap.tileSize, tileHashes: bitmap.hashes,
    frameHostCopyBytes: metrics.frameHostCopyBytes, canonicalHashedBytes: metrics.canonicalHashedBytes,
    canonicalRehashedTiles: metrics.canonicalRehashedTiles, nativePlaneEqualityReused: metrics.nativePlaneEqualityReused === true,
    externalHintMissedChange: metrics.externalHintMissedChange === true };
}
const root = mkdtempSync(join(tmpdir(), 'blitzen-canvas-review-'));
const log = join(root, 'commits.jsonl');
const committed = () => existsSync(log) ? readFileSync(log, 'utf8').trim().split('\n').map(line => JSON.parse(line)) : [];
const attempts = [];
let applicationHttpRequests = 0;
// The grant only covers the review controls and the canvas. Every route keeps
// those roots identifiable, including the authoritative GET after a lost reply.
const layout = controls => `<!doctype html>${sandboxedSoftware ? '<title>Blitzen ephemeral compositor fixture</title>' : ''}<main id="controls">${controls}</main>
  <canvas id="cell" style="isolation:isolate" width="8" height="8"></canvas><aside>Outside the review grant</aside>`;
let current = { job: 'A', revision: 1 };
const key = ({ job, revision }) => `${job}-r${revision}`;
const pathFor = (row) => `/queue/${key(row)}`;
const server = createServer((req, res) => {
  applicationHttpRequests++;
  res.setHeader('Cache-Control', 'no-store');
  if (req.method === 'POST') {
    attempts.push(req.url);
    if (req.url !== pathFor(current)) {
      res.writeHead(409, { 'content-type': 'text/html' });
      res.end(layout('<div id="conflict">Schedule changed</div>'));
      return;
    }
    appendFileSync(log, JSON.stringify(current) + '\n');
    res.writeHead(303, { location: '/accepted' });
    res.end();
    return;
  }
  res.setHeader('content-type', 'text/html');
  if (req.url === '/records') {
    res.end(layout(committed().map(row => `<div id="review-${key(row)}">Queued</div>`).join('')));
  } else if (req.url === '/accepted') {
    res.end(layout('Accepted'));
  } else {
    res.end(layout(`<form method="post" action="${pathFor(current)}"><button id="queue">Queue review</button></form>`)
      + "<script>const ctx = document.querySelector('canvas').getContext('2d'); ctx.fillStyle='red'; ctx.fillRect(0,0,8,8)</script>");
  }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
// Qualification publishes only phase metadata and counters. A refusal can occur
// before a page, a grant, or a repaint exists; those timestamps stay absent.
const progressPath = process.env.BLITZEN_RESIDENT_PROGRESS_FILE;
const progress = { phase: 'setup', pid: process.pid, nodeTimeOriginMs: performance.timeOrigin };
function checkpoint(fields) {
  Object.assign(progress, fields);
  if (!progressPath) return;
  writeFileSync(progressPath + '.partial', JSON.stringify(progress));
  renameSync(progressPath + '.partial', progressPath);
}
checkpoint({});
let service, adapter, captureStart, requestsBefore, ruleEvaluations = 0;
if (progressPath) process.once('SIGTERM', async () => {
  const deadline = setTimeout(() => process.exit(143), 5000);
  try { await service?.close(); } finally { clearTimeout(deadline); process.exit(143); }
});
try {
  const baseConfig = testBrowserSelection(root, { headless: !useCompositor, compositorTransport: compositor }).config;
  const config = { ...baseConfig, observation: { ...baseConfig.observation, canvasBackend } };
  if (observerMode !== undefined) {
    const traced = observerMode === 'on';
    if ((!traced && observerMode !== 'off') || !progressPath || sandboxedSoftware ||
        process.env.BLITZEN_RESIDENT_PHYSICAL !== '1' || !useCompositor ||
        process.env.BLITZEN_RESIDENT_SCOPE_PROBE || process.env.BLITZEN_RESIDENT_SCOPE_TRACE_PREFIX ||
        process.env.BLITZEN_CAPTURE_RUN_DIRECTORY || process.env.BLITZEN_RESIDENT_SCOPE_LOOKALIKE ||
        Boolean(frameTracePrefix) !== (traced && Boolean(config.browser.fawnBuild)) ||
        Boolean(stockTracePrefix) !== (traced && !config.browser.fawnBuild)) {
      throw Error('Matched observation requires a separate physical run with the planned trace mode');
    }
  } else if (stockTracePrefix) throw Error('Stock trace requires a matched observer run');
  service = await WorkspaceService.open({ stateDir: join(root, 'service'), config,
    createAdapter: options => {
      adapter = createPlaywrightAdapter({ ...options, restrictedPlaneEvidence: expectPlaneEvidence, requireSandbox: expectPlaneEvidence || sandboxedSoftware || process.env.BLITZEN_RESIDENT_PHYSICAL === '1',
        captureAttribution: Boolean(process.env.BLITZEN_CAPTURE_RUN_DIRECTORY), frameImportEvidence: Boolean(frameTracePrefix),
        privateWorldPerCapture: observerMode === 'on' });
      const snapshot = adapter.snapshot.bind(adapter);
      adapter.snapshot = async (...args) => {
        const result = await snapshot(...args);
        if (result.regionBitmapMetrics?.['#cell']) {
          captures.push(result.regionBitmapMetrics['#cell']);
          latestFixtureBitmap = result.regionBitmaps['#cell'];
          const frame = result.frameEvidence?.['#cell'];
          if (frame) latestCapture = captureReceipt(frame, latestFixtureBitmap, captures.at(-1));
        }
        return result;
      };
      const execute = adapter.execute.bind(adapter);
      adapter.execute = async (id, request, files) => {
        // A new revision lands after Blitzen's final check, before site commit.
        if (request.actionId === 'revision-race') current = { ...current, revision: current.revision + 1 };
        const result = await execute(id, request, files);
        if (request.actionId === 'lost-ack') throw Error('Test discarded the acknowledgment after commit');
        return result;
      };
      return adapter;
    } });
  const workspaceId = 'review';
  const admin = { workspaceId, ...await service.createWorkspace({ workspaceId, scopedReads: true }) };
  const owner = { workspaceId, ...await service.assignOwner({ ...admin, ownerId: 'reviewer' }) };
  const grant = { ...admin, ownerId: 'reviewer', pageIds: ['main'], targetOrigins: [origin], intents: ['queue-review'] };
  await service.grantAuthorization({ ...grant, grantId: 'open-local-site', readRegions: null, actionKinds: ['navigate'] });
  const observe = () => service.observe({ ...owner, regions: ['#cell'] });
  const submit = async (observation, spec) => (await service.submitAction({
    ...owner, authorizedIntent: 'queue-review', observationVersion: observation.observationVersion, ...spec,
  })).result;
  const navigate = async (id, path) => {
    const result = await submit(await service.observe(owner), { actionId: id, kind: 'navigate', target: { url: origin + path } });
    assert.equal(result.outcome, 'completed');
  };
  // This reviewed application encodes both identifiers in an immutable form
  // destination. A visually identical cell without that contract cannot act.
  const proposal = (observation, actionId) => {
    const target = observation.elements.find(element => element.selector === '#queue');
    const route = target?.formAction && new URL(target.formAction);
    const id = route?.origin === origin && /^\/queue\/([A-Z]-r\d+)$/.exec(route.pathname)?.[1];
    if (!id) return null;
    return { actionId, kind: 'submit', target: { handle: target.handle },
      observationScope: { kind: 'regions', selectors: ['#cell'] },
      effect: { external: true, effectKey: 'review-' + id },
      completion: { completedWhen: [{ kind: 'element-present', selector: '#review-' + id }],
        failedWhen: [{ kind: 'element-present', selector: '#conflict' }] } };
  };
  await navigate('open', '/schedule');
  await service.revokeAuthorization({ ...admin, grantId: 'open-local-site' });
  await service.grantAuthorization({ ...grant, grantId: 'review-regions', readRegions: ['#controls', '#cell'],
    actionKinds: ['navigate', 'submit'] });
  await assert.rejects(service.observe({ ...owner, regions: ['aside'] }), { code: 'unauthorized-target' });
  const page = adapter._page(workspaceId); // Fixture controls and independent compositor oracle only.
  const cell = page.locator('#cell');
  const red = () => cell.screenshot().then(png => assertSolidPixels(png, [255, 0, 0, 255]));
  const original = await observe();
  const originalImage = await red();
  assert.ok(original.regions['#cell']);
  assert.ok(!original.text.includes('Outside the review grant'));
  await cell.evaluate(el => { const ctx = el.getContext('2d'); ctx.fillStyle = 'blue'; ctx.fillRect(0, 0, 8, 8); });
  const pixelRefusal = await submit(original, proposal(original, 'repainted'));
  assert.equal(pixelRefusal.neverDispatched?.code, 'stale-observation');
  assert.equal(captures.at(-1).path, 'cpu', 'dispatch independently re-reads the selected pixels');
  const blue = await observe();
  assert.equal(blue.contentHash, original.contentHash, 'canvas-only repaint preserves markup and control values');
  assert.notEqual(blue.regions['#cell'], original.regions['#cell']);
  assertSolidPixels(await cell.screenshot(), [0, 0, 255, 255]);

  await cell.evaluate(el => {
    el.width = 130; el.height = 70;
    const ctx = el.getContext('2d'); ctx.fillStyle = 'red'; ctx.fillRect(0, 0, el.width, el.height);
  });
  const tiled = await observe();
  const unchanged = await service.observe({ ...owner, regions: ['#cell'], view: 'outline', sinceVersion: tiled.observationVersion });
  assert.deepEqual(unchanged.regions['#cell'], { unchanged: true });
  if (canvasBackend === 'webgpu') {
    assert.equal(captures.at(-1).comparedOnGpu, true);
    assert.equal(captures.at(-1).cpuPixelReadBytes, 0);
    assert.equal(captures.at(-1).gpuMaskReadbackBytes, 4);
  }
  await cell.evaluate(el => { const ctx = el.getContext('2d'); ctx.fillStyle = 'blue'; ctx.fillRect(129, 69, 1, 1); });
  const updated = await service.observe({ ...owner, regions: ['#cell'], view: 'outline', sinceVersion: tiled.observationVersion });
  assert.deepEqual(updated.regions['#cell'].pixels, { width: 130, height: 70, baseline: 'compared',
    changed: [{ x: 128, y: 64, width: 2, height: 6 }] });
  if (canvasBackend === 'webgpu') assert.equal(captures.at(-1).cpuPixelReadBytes, 2 * 6 * 4);
  const beforeAlpha = await observe();
  await cell.evaluate(el => { el.getContext('2d').clearRect(70, 5, 1, 1); });
  const alpha = await service.observe({ ...owner, regions: ['#cell'], view: 'outline', sinceVersion: beforeAlpha.observationVersion });
  assert.deepEqual(alpha.regions['#cell'].pixels.changed, [{ x: 64, y: 0, width: 64, height: 64 }]);
  assert.equal((await service.observe({ ...owner, regions: ['#cell'], view: 'outline',
    sinceVersion: alpha.observationVersion })).regions['#cell'].unchanged, true);

  await cell.evaluate(el => { el.width = 0; });
  assert.equal((await observe()).regions['#cell'], null, 'an empty bitmap cannot bind a region');
  await page.reload();
  const beforeSwap = await observe();
  current = { job: 'B', revision: 1 };
  await page.reload();
  assert.deepEqual(await red(), originalImage);
  const stale = await submit(beforeSwap, { ...proposal(beforeSwap, 'lookalike'), resubmissionOf: 'repainted' });
  assert.equal(stale.neverDispatched?.code, 'target-changed');
  await page.locator('form').evaluate(form => { form.action = '/queue'; });
  assert.equal(proposal(await observe(), 'unbound'), null);
  assert.deepEqual(attempts, []);

  await page.reload();
  const beforeRace = await observe();
  const raced = await submit(beforeRace, proposal(beforeRace, 'revision-race'));
  assert.equal(raced.outcome, 'failed');
  assert.deepEqual(attempts, ['/queue/B-r1']);
  assert.deepEqual(committed(), []);

  await navigate('reopen', '/schedule');
  await cell.evaluate(el => {
    el.width = 130; el.height = 70;
    const ctx = el.getContext('2d'); ctx.fillStyle = 'red'; ctx.fillRect(0, 0, 130, 70);
  });
  let session = 'start';
  const evidence = async sinceVersion => {
    const snapshot = await service.observe({ ...owner, regions: ['#cell'], sinceVersion,
      ...(useCompositor ? { compositorRegion: '#cell', compositorSession: session } : {}) });
    if (useCompositor) session = snapshot.frameEvidence['#cell'].sessionId;
    return snapshot;
  };
  checkpoint({ phase: 'authorize-surface', knownEffects: committed().length });
  const tracePrefix = process.env.BLITZEN_RESIDENT_SCOPE_TRACE_PREFIX;
  if (tracePrefix && process.env.BLITZEN_RESIDENT_SCOPE_PROBE !== '1') throw Error('Scope trace requires diagnostic mode');
  if (frameTracePrefix && (tracePrefix || process.env.BLITZEN_RESIDENT_SCOPE_PROBE ||
      process.env.BLITZEN_CAPTURE_RUN_DIRECTORY || !progressPath || compositor !== 'external-texture')) {
    throw Error('Ordinary frame tracing needs its own external-texture fixture');
  }
  if (process.env.BLITZEN_CAPTURE_RUN_DIRECTORY) {
    await awaitNativeObserver(page, process.env.BLITZEN_CAPTURE_RUN_DIRECTORY, config.browser.fawnBuild.runtime.library.path);
  }
  let stalePixelControl;
  if (useCompositor) {
    const paint = color => cell.evaluate((el, value) => {
      const ctx = el.getContext('2d'); ctx.fillStyle = value; ctx.fillRect(0, 0, el.width, el.height);
    }, color);
    // Let the restricted stream settle before pairing frames. This control runs outside the timed interval.
    await paint('rgb(113,5,0)');
    let base = await evidence();
    for (let i = 0; i < 5; i++) {
      base = await evidence(base.observationVersion);
      if (base.visualChanges['#cell']?.baseline === 'compared') break;
    }
    const first = latestCapture;
    let changedColor = 'rgb(114,6,0)';
    await paint(changedColor);
    let changed = await evidence(base.observationVersion);
    if (!changed.visualChanges['#cell']?.changed.length) {
      // Display capture itself may quantize away the source change. Record which pair the frame actually resolves.
      changedColor = 'lime'; await paint(changedColor);
      changed = await evidence(base.observationVersion);
    }
    assert.ok(changed.visualChanges['#cell']?.changed.length, 'restricted frame must contain the control change');
    await paint('rgb(113,5,0)');
    const staleBase = await evidence(), reviewed = latestCapture;
    assert.deepEqual(reviewed.tileHashes, first.tileHashes);
    await paint(changedColor);
    const effectsBefore = committed().length, attemptsBefore = attempts.length;
    const refusal = await submit(staleBase, proposal(staleBase, 'canonical-color-refusal'));
    const checked = latestCapture, effectsAfter = committed().length;
    const changedTiles = reviewed.tileHashes.flatMap((hash, i) => hash === checked.tileHashes[i] ? [] : [i]);
    assert.equal(refusal.neverDispatched?.code, 'stale-observation');
    assert.ok(changedTiles.length > 0 && checked.mediaTimestampUs > reviewed.mediaTimestampUs);
    assert.equal(checked.sessionId, reviewed.sessionId);
    assert.equal(attemptsBefore, attempts.length);
    assert.equal(effectsBefore, effectsAfter);
    stalePixelControl = { reviewed, checked, changedTiles, sourceColor: changedColor, refusal: refusal.neverDispatched.code,
      effectsBefore, effectsAfter };
    await paint('red');
  }
  const finishTrace = tracePrefix ? await startScopeTrace(page, tracePrefix) : null;
  const finishFrameTrace = frameTracePrefix ? await startScopeTrace(page, frameTracePrefix, frameEventNames) : null;
  const finishStockTrace = stockTracePrefix ? await startScopeTrace(page, stockTracePrefix, frameEventNames) : null;
  let frameScope;
  let beforeChange = await evidence();
  if (useCompositor) {
    for (let i = 0; i < 5; i++) {
      beforeChange = await evidence(beforeChange.observationVersion);
      if (beforeChange.visualChanges['#cell']?.baseline === 'compared') break;
    }
    assert.equal(beforeChange.visualChanges['#cell']?.baseline, 'compared');
  }
  if (progressPath) {
    const sessionCdp = await page.context().newCDPSession(page);
    try {
      const { targetInfo } = await sessionCdp.send('Target.getTargetInfo');
      const version = await sessionCdp.send('Browser.getVersion');
      let browserPid, gpuProcessPid, softwareSandbox, observedRuntime;
      if (process.env.BLITZEN_RESIDENT_OBSERVE_BROWSER_PID === '1') {
        const control = await page.context().browser().newBrowserCDPSession();
        try {
          const { processInfo } = await control.send('SystemInfo.getProcessInfo');
          const roots = processInfo.filter(row => row.type === 'browser');
          assert.equal(roots.length, 1, 'one browser root per fixture');
          browserPid = roots[0].id;
          const gpuProcesses = processInfo.filter(row => row.type.toLowerCase() === 'gpu');
          if (gpuProcesses.length === 1) gpuProcessPid = gpuProcesses[0].id;
          if (observerMode) {
            const { gpu } = await control.send('SystemInfo.getInfo');
            const flags = (await control.send('Browser.getBrowserCommandLine')).arguments;
            const selectors = flags.filter(flag => flag.startsWith('--use-webgpu-runtime='));
            const mode = config.browser.fawnBuild?.runtime.mode;
            const expected = mode ? ['--use-webgpu-runtime=' + (mode === 'doe-c' ? 'doe' : 'dawn')] : [];
            assert.deepEqual(selectors, expected);
            assert.equal(flags.includes('--fawn-resident-frame-provenance'), Boolean(frameTracePrefix));
            assert.equal(flags.includes('--fawn-resident-command-provenance'), false);
            assert.ok(gpuProcessPid && gpu.auxAttributes?.sandboxed === true, 'Observed sandboxed GPU process required');
            observedRuntime = { gpuProcessPid, sandboxed: true, selectors, frameObserver: Boolean(frameTracePrefix) };
          }
          if (sandboxedSoftware) {
            const { gpu } = await control.send('SystemInfo.getInfo');
            const flags = (await control.send('Browser.getBrowserCommandLine')).arguments;
            const restricted = /^--(?:no-sandbox|disable-(?:gpu|setuid|namespace|seccomp-filter)-sandbox|in-process-gpu|single-process|auto-accept-this-tab-capture|auto-select-desktop-capture-source|use-fake-ui-for-media-stream)(?:=|$)/;
            if (gpu.auxAttributes?.sandboxed !== true || !gpuProcessPid || flags.some(flag => restricted.test(flag))) {
              throw Error('Software diagnostic requires the browser GPU sandbox and an ordinary capture chooser');
            }
            softwareSandbox = { gpuSandboxed: true, gpuProcessPid, renderer: gpu.auxAttributes.glRenderer,
              devices: gpu.devices.map(({ vendorString, deviceString }) => ({ vendorString, deviceString })) };
          }
        } finally { await control.detach(); }
      }
      if (process.env.BLITZEN_RESIDENT_PHYSICAL === '1') {
        const flags = (await sessionCdp.send('Browser.getBrowserCommandLine')).arguments;
        if (flags.some(flag => /--(?:no-sandbox|disable-gpu(?:-sandbox)?|auto-accept-this-tab-capture|auto-select-desktop-capture-source|use-fake-ui-for-media-stream)(?:=|$)|^--enable-unsafe-swiftshader$|^--(?:use-angle|use-gl|use-vulkan)=.*(?:swiftshader|llvmpipe|lavapipe)/i.test(flag))) {
          throw Error('Physical browser qualification refused its launch flags');
        }
      }
      const entry = [...adapter.compositor.entries.values()].find(e => e.id === session);
      if (finishFrameTrace) {
        const { frameTree: { frame: document } } = await sessionCdp.send('Page.getFrameTree');
        const base = beforeChange.frameEvidence['#cell'];
        assert.ok(browserPid && document.loaderId === base.loaderId && entry.grantId === base.grantId);
        frameScope = { frameId: document.id, loaderId: document.loaderId, executionContextId: entry.contextId,
          grantId: entry.grantId, grantRevision: base.grantRevision, captureId: session };
      }
      const gpu = (await adapter.compositor.control(entry, 'adapterInfo')).result?.value ?? null;
      checkpoint({ phase: 'ready', targetId: targetInfo.targetId, surfaceId: session, browser: version, browserPid, gpuProcessPid,
        ...(frameScope ? { frameImportScope: frameScope } : {}), ...(softwareSandbox ? { softwareSandbox } : {}),
        ...(observedRuntime ? { observedRuntime } : {}), gpu, commandLabel: null,
        selection: config.browser.fawnBuild ?? { channel: config.browser.channel } });
    } finally { await sessionCdp.detach(); }
    const barrier = process.env.BLITZEN_RESIDENT_BARRIER_FILE;
    const deadline = performance.now() + 150000;
    while (barrier && !existsSync(barrier)) {
      if (performance.now() >= deadline) throw Error('Matched run did not release this process');
      await new Promise(resolve => setTimeout(resolve, 25));
    }
  }
  captureStart = captures.length; requestsBefore = applicationHttpRequests;
  checkpoint({ phase: 'measure' });
  const started = performance.now();
  const patch = useCompositor ? { x: 84, y: 40, length: 5 } : { x: 129, y: 69, length: 1 };
  const browserMutationEpochMs = await cell.evaluate((el, p) => {
    const ctx = el.getContext('2d'); ctx.fillStyle = 'blue'; ctx.fillRect(p.x, p.y, p.length, p.length);
    return performance.timeOrigin + performance.now();
  }, patch);
  const mutationReturned = performance.now();
  if (progressPath) checkpoint({ phase: 'observe-change', requestMonotonicMs: started, mutationReturnedMonotonicMs: mutationReturned, browserMutationEpochMs });
  const valid = await evidence(beforeChange.observationVersion);
  const changed = valid.visualChanges['#cell']?.changed ?? [];
  const reviewedBitmap = latestFixtureBitmap, freshReviewed = latestCapture;
  // Select the application action only if the expected indicator tile changed.
  // Independent revision and authority checks still apply to the actual POST.
  const frame = valid.frameEvidence?.['#cell'] ?? { width: 130, height: 70 };
  if (process.env.BLITZEN_RESIDENT_SCOPE_PROBE === '1' && useCompositor && compositor !== 'native-helper') {
    assert.equal(frame.nativeScopeRequest?.captureId, session);
    assert.ok(frame.nativeScopeRequest.executionContextId > 0 && frame.nativeScopeRequest.bracket > 0);
  }
  const x = Math.floor((patch.x + patch.length / 2) * frame.width / 130);
  const y = Math.floor((patch.y + patch.length / 2) * frame.height / 70);
  const tile = changed[0];
  ruleEvaluations++;
  const ruleMatched = changed.length === 1 && x >= tile.x && x < tile.x + tile.width && y >= tile.y && y < tile.y + tile.height;
  const accepted = ruleMatched ? proposal(valid, 'lost-ack') : null;
  assert.ok(accepted);
  if (stalePixelControl) accepted.resubmissionOf = 'canonical-color-refusal';
  if (progressPath) checkpoint({ phase: 'dispatch', imageCorrect: true, nativeScopeRequest: frame.nativeScopeRequest ?? null, commandLabel: captures.at(-1)?.diffCommandLabel ?? null, indicator: { expectedPixel: [x, y], changed } });
  let lookalike;
  if (process.env.BLITZEN_RESIDENT_SCOPE_LOOKALIKE === '1') {
    if (!finishTrace || !frame.nativeScopeRequest) throw Error('Lookalike control requires a private scope trace');
    lookalike = await exerciseLookalike(page, captures.at(-1).diffCommandLabel);
  }
  if (finishTrace) {
    // A page-authored event with copied values must never establish provenance.
    await page.evaluate(request => performance.mark('FawnResidentWorldSubmit', { detail: request }), frame.nativeScopeRequest);
    await finishTrace(frame.nativeScopeRequest);
  }
  const lost = await submit(valid, accepted);
  const freshChecked = latestCapture;
  assert.equal(lost.isUncertain, true, JSON.stringify(lost));
  assert.deepEqual(committed(), [{ job: 'B', revision: 2 }]);
  const reconcile = () => service.reconcile({ ...owner, actionId: 'lost-ack', method: 'application-state',
    effectPresentWhen: [{ kind: 'url-equals', value: origin + '/records' }, ...accepted.completion.completedWhen] });
  assert.equal((await reconcile()).reconciliation.result, 'inconclusive');
  await navigate('lookup', '/records');
  assert.equal((await reconcile()).reconciliation.result, 'effect-present');
  const verifiedAt = performance.now();
  const observer = observerMode ? { mode: observerMode, targetId: progress.targetId, captureId: session,
    browserPid: progress.browserPid, category: observerMode === 'on' ? traceCategory : null } : undefined;
  if (finishStockTrace) await finishStockTrace(observer);
  if (finishFrameTrace) {
    assert.equal(frame.sessionId, frameScope.captureId);
    assert.equal(frame.loaderId, frameScope.loaderId);
    assert.equal(frame.grantId, frameScope.grantId);
    assert.equal(frame.grantRevision, frameScope.grantRevision);
    assert.ok(Number.isSafeInteger(frame.mediaTimestampUs));
    assert.ok(Number.isSafeInteger(frame.frameImportOrdinal) && frame.frameImportOrdinal > 0);
    await finishFrameTrace({ ...frameScope, mediaTimestampUs: frame.mediaTimestampUs, ordinal: frame.frameImportOrdinal,
      width: frame.width, height: frame.height });
  }
  const counters = summarizeResidentOutcome({ elapsedMs: verifiedAt - started, verified: true,
    ruleEvaluations, modelCalls: [], captures: captures.slice(captureStart) });
  assert.equal(counters.plannerCalls, 0);
  assert.equal(counters.modelSpendUsd, 0);
  let pixelAuthority;
  if (useCompositor) {
    assert.deepEqual(freshChecked.tileHashes, freshReviewed.tileHashes);
    if (expectPlaneEvidence) {
      assert.equal(compositor,'external-texture');
      assert.ok(freshChecked.nativePlaneEqualityReused && !stalePixelControl.checked.nativePlaneEqualityReused,
        'Both the exact repeat and a newly changed real frame must reach the native eligibility check');
      assert.equal(freshChecked.frameHostCopyBytes,0);
      assert.equal(freshChecked.canonicalHashedBytes,0);
      assert.ok(stalePixelControl.checked.frameHostCopyBytes > 0);
    }
    assert.equal(freshChecked.sessionId, freshReviewed.sessionId);
    assert.ok(freshChecked.mediaTimestampUs > freshReviewed.mediaTimestampUs);
    pixelAuthority = { schema: 'blitzen.canonical-capture/v1', representation: frame.pixelRepresentation, stale: stalePixelControl,
      fresh: { reviewed: freshReviewed, checked: freshChecked, effectCount: committed().length, reconciliation: 'effect-present' } };
  }
  const receipt = { clock: 'request-repaint-through-authoritative-reconciliation', canvasBackend, ...(pixelAuthority ? { pixelAuthority } : {}),
    ...(observer ? { observer } : {}),
    ...(frameScope ? { diagnosticOnly: true, diagnosticKind: 'frame-import', frameImportTimestampUs: frame.mediaTimestampUs, frameImportOrdinal: frame.frameImportOrdinal,
      frameImportDimensions: [frame.width, frame.height] } : {}),
    ...(frame.nativeScopeRequest ? { nativeScopeRequest: frame.nativeScopeRequest, diagnosticOnly: true } : {}),
    ...(lookalike ? { mainWorldControl: lookalike } : {}),
    ...(useCompositor ? { digestMode: config.observation.compositorDigest ?? 'cpu', reviewedBitmap } : {}),
    compositorTransport: compositor ?? null, selection: config.browser.fawnBuild ?? { channel: config.browser.channel },
    applicationHttpRequests: applicationHttpRequests - requestsBefore, indicator: { expectedPixel: [x, y], changed },
    knownEffects: committed().length, ...counters };
  if (progressPath) {
    Object.assign(receipt, { requestMonotonicMs: started, mutationReturnedMonotonicMs: mutationReturned,
      browserMutationEpochMs, outcomeVerifiedMonotonicMs: verifiedAt, imageCorrect: true, committedRevision: key(committed()[0]) });
    checkpoint({ phase: 'reconciled', knownEffects: committed().length, outcomeVerifiedMonotonicMs: verifiedAt });
  }
  if (process.env.BLITZEN_RESIDENT_RECEIPT_FILE) {
    writeFileSync(process.env.BLITZEN_RESIDENT_RECEIPT_FILE, JSON.stringify(receipt, null, 2) + '\n', { flag: 'wx' });
  }
  console.log(JSON.stringify(receipt));
  await navigate('return', '/schedule');
  const after = await observe();
  await assert.rejects(submit(after, proposal(after, 'duplicate')), { code: 'effect-already-completed' });
  assert.deepEqual(attempts, ['/queue/B-r1', '/queue/B-r2']);
  assert.deepEqual(committed(), [{ job: 'B', revision: 2 }]);
  console.log('ok: canvas-only repaint and identical-pixel job swap refused before POST, revision race rejected at site, one committed review recovered without repeat POST');
} catch (error) {
  const partial = captureStart === undefined ? { plannerCalls: 0, modelSpendUsd: 0 } :
    { ...summarizeResidentOutcome({ elapsedMs: null, verified: false, ruleEvaluations, modelCalls: [], captures: captures.slice(captureStart) }),
      applicationHttpRequests: applicationHttpRequests - requestsBefore };
  checkpoint({ failure: error.message, failureType: error.name, knownEffects: committed().length, totalApplicationHttpRequests: applicationHttpRequests, partial });
  throw error;
} finally {
  await service?.close();
  await new Promise(resolve => server.close(resolve));
  rmSync(root, { recursive: true, force: true });
}
