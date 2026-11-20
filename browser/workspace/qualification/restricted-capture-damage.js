// Run only with a disposable local display and its controlled current-tab chooser.
// The probe observes the native-helper copy in its existing isolated world.
// Its measurements never enter the evidence, damage decision, or dispatcher.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createCdpAdapter } from '../src/adapters/cdp-adapter.js';
import { WorkspaceService } from '../src/service.js';
import { testBrowserSelection } from '../test/support/browser-selection.js';

async function listen(html) {
  const server = createServer((_req, res) => {
    res.setHeader('content-type', 'text/html');
    res.end(html);
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  return { origin: 'http://127.0.0.1:' + server.address().port,
    close: () => new Promise(resolve => server.close(resolve)) };
}

function witnessCopy() {
  const prototype = VideoFrame.prototype;
  const descriptor = Object.getOwnPropertyDescriptor(prototype, 'copyTo');
  const rows = [];
  Object.defineProperty(prototype, 'copyTo', { ...descriptor, value: function (...args) {
    const metadata = typeof this.metadata === 'function' ? this.metadata() : null;
    if (rows.length >= 32) throw Error('Exceeded fixture sample count');
    rows.push({ format: this.format, width: this.displayWidth, height: this.displayHeight,
      rotation: this.rotation, flip: this.flip, nativeScale: this.visibleRect.width === this.displayWidth
        && this.visibleRect.height === this.displayHeight,
      wrapperMetadataFields: Object.keys(metadata ?? {}).sort(),
      wrapperHasCounter: metadata !== null && ('counter' in metadata || 'captureCounter' in metadata),
      wrapperHasDamage: metadata !== null && ('updateRect' in metadata || 'update_rect' in metadata),
      destinationBytes: args[0].byteLength });
    return descriptor.value.apply(this, args);
  } });
  return () => { Object.defineProperty(prototype, 'copyTo', descriptor); return rows; };
}

if (process.env.BLITZEN_TEST_ELEMENT_CAPTURE !== '1') throw Error('Use the controlled Element Capture fixture display');
const directory = mkdtempSync(join(tmpdir(), 'blitzen-capture-damage-'));
const sibling = await listen('<body style="background:blue"><script>onmessage=e=>{if(e.source===parent&&e.data==="paint"){document.body.style.background="lime";requestAnimationFrame(()=>requestAnimationFrame(()=>parent.postMessage("painted",e.origin)))}}</script>');
const site = await listen('<title>Blitzen restricted capture fixture</title><style>body{margin:0}canvas{isolation:isolate;position:absolute;inset:0;z-index:1}iframe{border:0;position:absolute;inset:0 auto auto 0;width:64px;height:64px;z-index:2;pointer-events:none}button{position:fixed;top:100px}</style>'
  + '<canvas id="surface" width="128" height="64"></canvas><iframe src="'+sibling.origin+'"></iframe><button id="ack" onclick="this.textContent=\'done\'">ack</button>'
  + '<script>const ctx=document.querySelector("canvas").getContext("2d");ctx.fillStyle="red";ctx.fillRect(0,0,128,64)</script>');
let service, adapter;
try {
  const { config } = testBrowserSelection(directory, { headless: false, compositorTransport: 'native-helper' });
  const metrics = [];
  service = await WorkspaceService.open({ stateDir: directory, config, createAdapter(options) {
    adapter = createCdpAdapter(options);
    const snapshot = adapter.snapshot.bind(adapter);
    adapter.snapshot = async (...args) => {
      const result = await snapshot(...args);
      const measured = result.regionBitmapMetrics?.['#surface'];
      if (result.frameEvidence?.['#surface'] && measured) metrics.push(measured);
      return result;
    };
    return adapter;
  } });
  const workspaceId = 'restricted';
  const admin = { workspaceId, ...await service.createWorkspace({ workspaceId, scopedReads: true }) };
  const owner = { workspaceId, ...await service.assignOwner({ ...admin, ownerId: 'observer' }) };
  const grant = { ...admin, ownerId: 'observer', pageIds: ['main'], targetOrigins: [site.origin],
    intents: ['inspect'], actionKinds: ['navigate', 'click'] };
  await service.grantAuthorization({ ...grant, grantId: 'navigation', readRegions: null });
  const first = await service.observe(owner);
  const admitted = await service.submitAction({ ...owner, actionId: 'open', kind: 'navigate', authorizedIntent: 'inspect',
    target: { url: site.origin }, observationVersion: first.observationVersion });
  assert.equal((await admitted.result).outcome, 'completed');
  let session = 'start';
  async function observe(sinceVersion) {
    const result = await service.observe({ ...owner, regions: ['#surface','#ack'], compositorRegion: '#surface',
      compositorSession: session, sinceVersion });
    session = result.frameEvidence['#surface'].sessionId;
    return result;
  }
  await assert.rejects(observe(), { code: 'unauthorized-target' });
  await service.revokeAuthorization({ ...admin, grantId: 'navigation' });
  await service.grantAuthorization({ ...grant, grantId: 'region', readRegions: ['#surface','#ack'] });
  let anchor = await observe();
  const entry = [...adapter.compositor.entries.values()].find(e => e.id === session);
  assert(entry && entry.grantId === 'region');
  async function isolated(expression) {
    const reply = await entry.send('Runtime.evaluate', { contextId: entry.contextId, expression, returnByValue: true });
    assert(!reply.exceptionDetails);
    return reply.result.value;
  }
  await isolated('globalThis.finishPrivateDamageProbe = (' + witnessCopy.toString() + ')(); true');
  for (let i = 0; i < 5; i++) {
    const next = await observe(anchor.observationVersion);
    anchor = next;
    if (next.visualChanges['#surface'].changed.length === 0) break;
  }

  const evaluate = fn => adapter._evaluate(workspaceId, '(' + fn.toString() + ')()');
  await evaluate(async () => {
    const frame = document.querySelector('iframe'), origin = new URL(frame.src).origin;
    if (origin === location.origin) throw Error('Expected a distinct origin');
    await new Promise(resolve => {
      addEventListener('message', function receive(event) {
        if (event.source === frame.contentWindow && event.origin === origin && event.data === 'painted') {
          removeEventListener('message', receive); resolve();
        }
      });
      frame.contentWindow.postMessage('paint', origin);
    });
    return true;
  });
  const outside = await observe(anchor.observationVersion);
  assert.equal(outside.visualChanges['#surface'].changed.length, 0);
  await evaluate(() => { ctx.fillStyle='lime'; ctx.fillRect(9,9,4,4); });
  const changed = await observe(outside.observationVersion);
  const changedTiles = changed.visualChanges['#surface'].changed.length;
  const { width, height } = changed.frameEvidence['#surface'];
  assert(changedTiles > 0 && changedTiles < Math.ceil(width/64)*Math.ceil(height/64));
  const sparse = metrics.at(-1);
  assert.equal(sparse.nativeHelperInputBytes, width*height*4);

  await evaluate(() => { ctx.fillStyle='blue'; ctx.fillRect(9,9,4,4); });
  const stale = await service.submitAction({ ...owner, actionId:'viewed-before-change', kind:'click', authorizedIntent:'inspect',
    observationVersion: changed.observationVersion, target:{ handle:changed.elements.find(e=>e.selector==='#ack').handle },
    observationScope:{ kind:'regions',selectors:['#surface'] } });
  assert.equal((await stale.result).neverDispatched?.code, 'stale-observation');
  assert.equal(await evaluate(() => document.querySelector('#ack').textContent), 'ack');
  const samples = await isolated('finishPrivateDamageProbe()');
  assert(samples.length >= 4 && samples.every(s => s.destinationBytes === s.width*s.height*4));
  assert(metrics.some(m => m.discardedFrames === 1));
  assert.equal(metrics.reduce((n,m) => n + m.permissionRequests,0),1);
  await service.revokeAuthorization({ ...admin, grantId:'region' });
  await assert.rejects(observe(), { code:'unauthorized-target' });
  console.log(JSON.stringify({ mode:'original-restricted-wrapper', outsideChangedTiles:0, changedTiles,
    width,height, sparseCopiedBytes:sparse.nativeHelperInputBytes, sparseHashedBytes:sparse.nativeHashedBytes,
    staleAction:'never-dispatched', grantRevocation:'refused', samples }));
} finally {
  await service?.close(); await site.close(); await sibling.close(); rmSync(directory,{recursive:true,force:true});
}
