// Browser-only closure. Only the isolated world owns the MediaStream or frames.
import { stockFramePlaneEvidence } from './stock-frame-planes.js';
import { randomUUID } from 'node:crypto';
import { createResidentCanvasReader } from './resident-canvas.js';
import { createGpuTileDigest } from './gpu-tile-sha256.js';
import { NativeFrameHelper } from './native-frame-helper.js';
import { queryPage } from './page-query.js';
import { canonicalJson, sha256Hex } from '../journal.js';

// Serialized into the isolated capture world. An external texture can discard visible color
// distinctions during conversion. Only canonical RGBA or its verified digest can clear a tile.
export async function canonicalRgbaTiles(bytes, width, height, tileSize, previous) {
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 1 || height < 1
      || width * height > 4 * 1024 * 1024 || bytes.length !== width * height * 4
      || !Number.isSafeInteger(tileSize) || tileSize < 1 || Math.ceil(width / tileSize) * Math.ceil(height / tileSize) > 256) {
    throw Error('Invalid restricted RGBA tile geometry');
  }
  const digest = async input => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', input)),
    value => value.toString(16).padStart(2, '0')).join('');
  const frameDigest = await digest(bytes);
  const kept = previous?.width === width && previous.height === height && previous.tileSize === tileSize
    && previous.frameDigest === frameDigest;
  if (kept) return { record: previous, hashedBytes: bytes.length, tilesHashed: 0 };
  const hashes = [];
  let hashedBytes = bytes.length;
  if (width <= tileSize && height <= tileSize) hashes.push(frameDigest);
  else {
    for (let y = 0; y < height; y += tileSize) {
      const row = [];
      for (let x = 0; x < width; x += tileSize) {
        const w = Math.min(tileSize, width - x), h = Math.min(tileSize, height - y);
        const packed = new Uint8Array(w * h * 4);
        for (let dy = 0; dy < h; dy++) {
          const start = ((y + dy) * width + x) * 4;
          packed.set(bytes.subarray(start, start + w * 4), dy * w * 4);
        }
        hashedBytes += packed.byteLength;
        row.push(digest(packed));
      }
      hashes.push(...await Promise.all(row));
    }
  }
  return { record: { width, height, tileSize, frameDigest, hashes }, hashedBytes, tilesHashed: hashes.length };
}

// The optional native witness is issued by the current restricted reader. Its serial
// names byte-identical I420 conversion inputs within that reader, never an image.
export async function canonicalRestrictedFrame(frame, width, height, tileSize, prior, copyRgba, canonicalTiles, stockEvidence) {
  const plan = stockEvidence ? stockEvidence.plan : frame.fawnRestrictedPlaneTiles?.(prior?.witness ?? null, prior?.serial ?? 0, tileSize);
  const serial = plan === undefined ? frame.fawnRestrictedPlaneVersion?.() ?? 0 : plan[0] ?? 0;
  const snapshotBytesCopied = plan?.[2] ?? null;
  const compatible = prior?.record.width === width && prior.record.height === height && prior.record.tileSize === tileSize;
  const bound = compatible && prior.serial > 0 && plan?.[1] === prior.serial;
  const dirty = bound ? plan.slice(3) : null;
  const cols = Math.ceil(width / tileSize), count = cols * Math.ceil(height / tileSize);
  const valid = dirty && prior.record.hashes.length === count && count <= 256 &&
    dirty.every((n, i) => Number.isInteger(n) && n >= 0 && n < count && (!i || dirty[i - 1] < n));
  // The closed wrapper retains only its native lease identity. Frames are closed by the caller.
  const witness = stockEvidence || plan === undefined || !serial ? null : frame;
  const common = { serial, witness, snapshotBytesCopied, usedNativeEquality: false, usedNativeTiles: false };
  if (compatible && serial > 0 && prior.serial === serial && (plan === undefined || (valid && !dirty.length))) {
    return { ...common, record: prior.record, copiedBytes: 0, hashedBytes: 0, tilesHashed: 0, usedNativeEquality: true };
  }
  if (valid && serial !== prior.serial && dirty.length && dirty.length < count) {
    const hashes = prior.record.hashes.slice();
    let copiedBytes = 0, hashedTiles = 0;
    for (const index of dirty) {
      const x = (index % cols) * tileSize, y = Math.floor(index / cols) * tileSize;
      const rect = { x, y, width: Math.min(tileSize, width - x), height: Math.min(tileSize, height - y) };
      let pixels;
      try { pixels = await copyRgba(frame, width, height, rect); }
      catch {
        // This browser rejected a tile read. Trust only a fresh full conversion.
        const full = await copyRgba(frame, width, height);
        const result = await canonicalTiles(full, width, height, tileSize, prior.record);
        return { ...common, ...result, copiedBytes: copiedBytes + full.length,
          hashedBytes: copiedBytes + result.hashedBytes, tilesHashed: hashedTiles + result.tilesHashed,
          rejectedTileReads: 1 };
      }
      if (pixels.length !== rect.width * rect.height * 4) throw Error('Incomplete canonical tile');
      copiedBytes += pixels.length;
      hashedTiles++;
      hashes[index] = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', pixels)),
        value => value.toString(16).padStart(2, '0')).join('');
    }
    // Tile hashes are authoritative. A whole-frame SHA cannot be updated from tile digests.
    return { ...common, record: { width, height, tileSize, frameDigest: null, hashes },
      copiedBytes, hashedBytes: copiedBytes, tilesHashed: dirty.length, usedNativeTiles: true };
  }
  const pixels = await copyRgba(frame, width, height);
  const result = await canonicalTiles(pixels, width, height, tileSize, prior?.record);
  return { ...result, ...common, copiedBytes: pixels.length };
}

function createElementCapture(id, resident, capture, leaseMs, scopeProbe, canonicalTiles, restrictedFrame, stockPlanes) {
  const { selector, origin } = capture;
  let element, stream, track, reader, observer, watchdog, expiry, ended = false;
  let verifiedBrackets = 0, previousCanonical = null, previousPlanes = null;
  const planeLease = {};
  async function copyRgba(frame, width, height, tile) {
    const bounds = frame.visibleRect;
    if (width < 1 || height < 1 || width * height > 4 * 1024 * 1024
      || bounds.width !== width || bounds.height !== height || frame.rotation || frame.flip) {
      throw Error('RGBA evidence requires a bounded, unrotated restricted frame at native scale');
    }
    if (tile && (!Number.isSafeInteger(tile.x) || !Number.isSafeInteger(tile.y)
      || !Number.isSafeInteger(tile.width) || !Number.isSafeInteger(tile.height)
      || tile.x < 0 || tile.y < 0 || tile.width < 1 || tile.height < 1
      || tile.x + tile.width > width || tile.y + tile.height > height)) throw Error('Tile outside restricted frame');
    const rect = tile ? { ...tile, x: bounds.x + tile.x, y: bounds.y + tile.y } : bounds;
    const pixels = new Uint8Array(rect.width * rect.height * 4);
    await frame.copyTo(pixels, { rect, format: 'RGBA', colorSpace: 'srgb', layout: [{ offset: 0, stride: rect.width * 4 }] });
    return pixels;
  }
  const stop = () => {
    if (ended) return;
    ended = true; previousCanonical = null; previousPlanes = null;
    clearTimeout(watchdog); clearTimeout(expiry); observer?.disconnect();
    removeEventListener('pagehide', stop); document.removeEventListener('freeze', stop);
    globalThis.navigation?.removeEventListener('navigate', stop);
    stream?.getTracks().forEach(t => t.stop());
    reader?.cancel().catch(() => {});
    resident.dispose();
  };
  const isCurrent = () => !ended && location.origin === origin && element?.isConnected
    && document.querySelectorAll(selector).length === 1 && document.querySelector(selector) === element;
  function assertCurrent() {
    if (!isCurrent() || (track && track.readyState !== 'live')) { stop(); throw Error('Selected surface or sharing permission ended'); }
  }
  function readGeometry() {
    const { x, y, width, height } = element.getBoundingClientRect();
    const scale = window.devicePixelRatio;
    if (![x, y, width, height, scale].every(Number.isFinite) || width <= 0 || height <= 0 || scale <= 0) {
      throw Error('Selected surface has no valid viewport geometry');
    }
    return { x, y, width, height, scale };
  }
  function renew() {
    if (ended) return false;
    clearTimeout(watchdog); watchdog = setTimeout(stop, leaseMs);
    return true;
  }
  renew();
  addEventListener('pagehide', stop, { once: true }); document.addEventListener('freeze', stop, { once: true });
  globalThis.navigation?.addEventListener('navigate', stop, { once: true });
  if (capture.ttlMs !== null) {
    const expiresAt = performance.now() + capture.ttlMs;
    const check = () => {
      const ms = expiresAt - performance.now();
      if (ms <= 0) stop(); else expiry = setTimeout(check, Math.min(ms, 2147483647));
    };
    check();
  }
  let permissionRequests = 0;
  async function start(selected) {
    element = selected;
    assertCurrent();
    if (!(element instanceof HTMLCanvasElement) || !globalThis.RestrictionTarget?.fromElement
      || !globalThis.MediaStreamTrackProcessor) throw Error('Selected compositor surface unavailable');
    observer = new MutationObserver(() => { if (!isCurrent()) stop(); });
    observer.observe(document, { subtree: true, childList: true, attributes: true });
    const target = await RestrictionTarget.fromElement(element);
    assertCurrent();
    permissionRequests++;
    stream = await navigator.mediaDevices.getDisplayMedia({
      preferCurrentTab: true, surfaceSwitching: 'exclude', audio: false, video: { displaySurface: 'browser', cursor: 'never' },
    });
    if (ended) { stream.getTracks().forEach(t => t.stop()); throw Error('Sharing request expired'); }
    [track] = stream.getVideoTracks();
    if (!track?.restrictTo || track.getSettings().displaySurface !== 'browser') throw Error('Capture requires the current browser tab');
    track.addEventListener('ended', stop, { once: true }); stream.addEventListener('inactive', stop, { once: true });
    await track.restrictTo(target);
    assertCurrent();
    await track.applyConstraints({ frameRate: { min: 5, max: 15 } });
    assertCurrent();
    reader = new MediaStreamTrackProcessor({ track, maxBufferSize: 1 }).readable.getReader();
  }
  async function sample(selected, verify, timeoutMs) {
    const newStream = !element;
    let timer, diagnosticPhase = 'open';
    async function read() {
      if (newStream) await start(selected);
      if (element !== selected) throw Error('The selected element was replaced');
      assertCurrent();
      // Geometry comes from the isolated DOM world, sampled on both sides of the frame read.
      const geometry = readGeometry();
      // Discard the queued frame and consume the next restricted compositor update.
      let closedFrames = 0;
      if (!newStream) {
        diagnosticPhase = 'discard prior frame';
        const queued = await reader.read();
        if (queued.done) throw Error('Sharing ended');
        queued.value.close(); closedFrames++;
      }
      diagnosticPhase = 'fresh restricted frame';
      const next = await reader.read();
      if (next.done) throw Error('Sharing ended');
      const frame = next.value;
      try {
        assertCurrent();
        const width = frame.displayWidth, height = frame.displayHeight;
        const bitmapMetrics = { permissionRequests: newStream ? permissionRequests : 0,
          consumedFrames: closedFrames + 1, discardedFrames: closedFrames };
        const evidence = { bitmapMetrics, frame: { source: 'element-capture', surfaceId: id, sessionId: id,
          transport: capture.transport, pixelRepresentation: 'video-frame-copy-to-srgb-rgba8', sourcePixelFormat: frame.format ?? null, mediaTimestampUs: frame.timestamp, width, height } };
        if (capture.transport === 'stock-i420') {
          let tileSize = 64;
          while (Math.ceil(width / tileSize) * Math.ceil(height / tileSize) > 256) tileSize *= 2;
          const input = await stockPlanes(frame, width, height, tileSize, previousPlanes, planeLease, capture.stockPlaneSource);
          assertCurrent();
          const confirmed = await restrictedFrame(frame, width, height, tileSize, previousCanonical, copyRgba, canonicalTiles, input);
          assertCurrent();
          previousPlanes = input; previousCanonical = confirmed;
          evidence.bitmap = { format: 'compositor-element-canonical-srgb-rgba8-v2', width, height, tileSize, hashes: confirmed.record.hashes };
          Object.assign(bitmapMetrics, { path: 'element-capture-stock-i420', stockPlaneSource: capture.stockPlaneSource,
            stockPlaneExportBytes: input.exportedBytes, stockPlaneEqualityReused: confirmed.usedNativeEquality,
            stockPlaneSparseTiles: confirmed.usedNativeTiles, stockCanonicalRgbaCopyBytes: confirmed.copiedBytes,
            frameHostCopyBytes: input.exportedBytes + confirmed.copiedBytes,
            canonicalHashedBytes: confirmed.hashedBytes, canonicalRehashedTiles: confirmed.tilesHashed, cpuPixelReadBytes: 0 });
        } else if (capture.transport === 'native-helper') {
          const pixels = await copyRgba(frame, width, height);
          evidence.restrictedRgba = pixels.toBase64();
          Object.assign(bitmapMetrics, { path: 'element-capture-native-helper', cpuPixelReadBytes: 0, frameHostCopyBytes: pixels.length,
            cdpPixelPayloadBytes: evidence.restrictedRgba.length, nativeHelperInputBytes: pixels.length });
        } else {
          // Chromium's native getSettings instrumentation can witness the
          // actual track and calling world. This read cannot change the crop.
          const bracket = scopeProbe ? async submit => {
            const witness = () => {
              assertCurrent();
              if (track.getSettings().displaySurface !== 'browser') throw Error('Capture source changed');
            };
            witness();
            diagnosticPhase = 'GPU queue completion';
            await submit();
            witness();
            evidence.frame.captureProofBracket = ++verifiedBrackets;
          } : null;
          diagnosticPhase = 'process restricted frame';
          const result = await resident.readFrame(element, frame, verify, capture.transport, bracket);
          const { width, height, tileSize, hashes, ...metrics } = result;
          let authority = hashes;
          Object.assign(bitmapMetrics, { path: 'element-capture-webgpu', ...metrics });
          if (capture.transport === 'external-texture') {
            // The GPU mask is a hint. Only an exact scope-bound native-plane
            // witness can reuse the prior digest without a canonical RGBA read.
            const canonical = await restrictedFrame(frame, width, height, tileSize, previousCanonical, copyRgba, canonicalTiles);
            assertCurrent();
            const hintMissedChange = Boolean(previousCanonical && metrics.comparedOnGpu && metrics.rehashedTiles === 0
              && (previousCanonical.record.hashes.length !== canonical.record.hashes.length
                || canonical.record.hashes.some((hash, index) => hash !== previousCanonical.record.hashes[index])));
            previousCanonical = canonical;
            authority = canonical.record.hashes;
            Object.assign(bitmapMetrics, { externalHintMissedChange: hintMissedChange,
              nativePlaneSparseTilesReused: canonical.usedNativeTiles, nativePlaneSnapshotCopyBytes: canonical.snapshotBytesCopied,
              rejectedCanonicalTileReads: canonical.rejectedTileReads ?? 0,
              nativePlaneEqualitySupported: canonical.serial > 0, nativePlaneEqualityReused: canonical.usedNativeEquality,
              nativePlaneLogicalBytesComparedOnReuse: canonical.usedNativeEquality ? 3 * width * height : 0,
              frameHostCopyBytes: canonical.copiedBytes, canonicalHashedBytes: canonical.hashedBytes,
              canonicalRehashedTiles: canonical.tilesHashed });
          }
          if (capture.transport === 'rgba-upload') {
            bitmapMetrics.canonicalHashedBytes = metrics.logicalTileHashBytes;
            bitmapMetrics.canonicalRehashedTiles = metrics.rehashedTiles;
          }
          evidence.bitmap = { format: 'compositor-element-canonical-srgb-rgba8-v2', width, height, tileSize, hashes: authority };
        }
        assertCurrent();
        const endGeometry = readGeometry();
        if (Object.keys(geometry).some(key => geometry[key] !== endGeometry[key])) {
          throw Error('Selected surface geometry changed during capture');
        }
        const { x, y, width: cssWidth, height: cssHeight, scale } = geometry;
        evidence.frame.binding = { deviceScaleFactor: scale, viewportRect: { x, y, width: cssWidth, height: cssHeight } };
        return evidence;
      } finally { frame.close(); }
    }
    try {
      if (ended) throw Error('Sharing ended; authorize a new capture');
      return await Promise.race([read(), new Promise((_, reject) => {
        timer = setTimeout(() => { stop(); reject(Error('Timed out waiting for restricted compositor evidence' + (scopeProbe ? ' at ' + diagnosticPhase : ''))); }, timeoutMs);
      })]);
    } catch (error) { stop(); throw error; } finally { clearTimeout(timer); }
  }
  return { id, sample, renew, stop, adapterInfo: resident.adapterInfo, status: () => ({ ended, trackState: track?.readyState, permissionRequests }) };
}

// One restricted capture per workspace page. The live authority is deliberately
// absent from disk. Replaying an old id after a restart cannot open a chooser.
export class CompositorSessions {
  entries = new Map();
  constructor({ privateWorldPerCapture = false } = {}) { this.privateWorldPerCapture = privateWorldPerCapture; }

  async end(entry) {
    if (this.entries.get(entry.key) !== entry) return;
    this.entries.delete(entry.key); clearInterval(entry.heartbeat); clearTimeout(entry.hostExpiry);
    const workerEnded = entry.helper?.close();
    let timer;
    try {
      // Handoff and shutdown must finish even if the browser pipe is wedged.
      // With no renewals the renderer's own lease also retires the track.
      await Promise.race([this.control(entry, 'stop'), new Promise(resolve => { timer = setTimeout(resolve, 1000); })]);
    } catch { /* A closed frame has already relinquished its native capture. */ }
    finally { clearTimeout(timer); await workerEnded; }
  }

  async release(workspaceId, grantId) {
    const matches = [...this.entries.values()].filter(e => (workspaceId === undefined || e.workspaceId === workspaceId)
      && (grantId === undefined || e.grantId === grantId));
    await Promise.all(matches.map(entry => this.end(entry)));
  }

  control(entry, method) {
    return entry.send('Runtime.evaluate', { contextId: entry.contextId, returnByValue: true,
      expression: `globalThis.__blitzenSurface?.id === ${JSON.stringify(entry.id)} && globalThis.__blitzenSurface.${method}()` });
  }

  async snapshot(workspaceId, pageId, send, args, capture, signal) {
    signal?.throwIfAborted();
    const key = JSON.stringify([workspaceId, pageId]);
    const { frameTree } = await send('Page.getFrameTree', {});
    signal?.throwIfAborted();
    const { loaderId, id: frameId } = frameTree.frame;
    const scope = JSON.stringify([loaderId, capture.origin, capture.ownerGeneration, capture.grantId, capture.grantRevision,
      capture.readRoots, capture.selector, capture.transport, capture.digest]);
    let entry = this.entries.get(key);
    let start = capture.session === 'start';
    if (start && entry?.scope === scope) {
      const state = (await this.control(entry, 'status').catch(() => null))?.result?.value;
      signal?.throwIfAborted();
      if (state?.trackState === 'live' && !state.ended) start = false;
    }
    if (capture.session !== 'start' && (!entry || capture.session !== entry.id || entry.scope !== scope)) {
      throw Error('Capture session expired or changed');
    }
    if (start) {
      if (entry) await this.end(entry);
      const id = randomUUID();
      const scopeProbe = process.env.BLITZEN_RESIDENT_SCOPE_PROBE === '1';
      const worldName = (scopeProbe || this.privateWorldPerCapture) ? 'Blitzen restricted capture ' + id : 'Blitzen selected compositor surface';
      const { executionContextId } = await send('Page.createIsolatedWorld', { frameId, worldName });
      signal?.throwIfAborted();
      entry = { key, workspaceId, grantId: capture.grantId, scope, id, scopeProbe, proofBracket: 0, send, contextId: executionContextId };
      this.entries.set(key, entry);
    }
    const abort = () => { void this.end(entry); };
    signal?.addEventListener('abort', abort, { once: true });
    try {
      signal?.throwIfAborted();
      if (start) {
        const leaseMs = 8000;
        const init = await send('Runtime.evaluate', { contextId: entry.contextId, returnByValue: true, expression: `(() => {
          globalThis.__blitzenSurface?.stop();
          globalThis.__blitzenSurface = (${createElementCapture.toString()})(${JSON.stringify(entry.id)},
            (${createResidentCanvasReader.toString()})(${JSON.stringify(entry.id)},
              ${capture.digest === 'webgpu' ? '(' + createGpuTileDigest.toString() + ')()' : 'null'}), ${JSON.stringify(capture)}, ${leaseMs}, ${entry.scopeProbe}, (${canonicalRgbaTiles.toString()}), (${canonicalRestrictedFrame.toString()}), (${stockFramePlaneEvidence.toString()})); return true;
        })()` });
        if (signal?.aborted || this.entries.get(key) !== entry) {
          await this.control(entry, 'stop').catch(() => {});
          throw Error('Capture request ended during initialization');
        }
        if (init.exceptionDetails) throw Error('Could not initialize restricted surface capture');
        // A stalled CDP connection can leave the renderer unable to acknowledge
        // renewal. Independently retire the helper and its retained pixels.
        const renewHost = () => {
          clearTimeout(entry.hostExpiry);
          entry.hostExpiry = setTimeout(() => { void this.end(entry); }, leaseMs);
          entry.hostExpiry.unref();
        };
        renewHost();
        let pending = false;
        entry.heartbeat = setInterval(async () => {
          if (pending) return;
          pending = true;
          try {
            const reply = await this.control(entry, 'renew');
            if (reply.result?.value !== true) await this.end(entry);
            else if (this.entries.get(key) === entry) renewHost();
          } catch { await this.end(entry); } finally { pending = false; }
        }, leaseMs / 4);
        entry.heartbeat.unref();
      }
      const response = await send('Runtime.callFunctionOn', { executionContextId: entry.contextId,
        functionDeclaration: `function(args, capture, id) {
          const current = globalThis.__blitzenSurface;
          if (!current || current.id !== id) throw Error('Capture session ended');
          return (${queryPage.toString()})({ ...args, captureSelector: capture.selector,
            captureFrame: element => current.sample(element, capture.verify, capture.timeoutMs) });
        }`, arguments: [{ value: args }, { value: capture }, { value: entry.id }],
        userGesture: start, awaitPromise: true, returnByValue: true });
      if (response.exceptionDetails) throw Error(response.exceptionDetails.exception?.description ?? response.exceptionDetails.text);
      const raw = response.result.value, selected = raw?.regions.find(region => region.selector === capture.selector && region.frame);
      if (!selected || this.entries.get(key) !== entry) throw Error('No live restricted compositor frame');
      if (capture.transport === 'native-helper') {
        const { width, height } = selected.frame;
        entry.helper ??= new NativeFrameHelper();
        const processing = entry.helper.sample(width, height, selected.restrictedRgba, capture.verify);
        delete selected.restrictedRgba;
        const { hashes, tileSize, hashedBytes, rehashedTiles, peakRgbaBytes } = await processing;
        const state = (await this.control(entry, 'status')).result?.value;
        if (this.entries.get(key) !== entry || state?.ended || state?.trackState !== 'live') throw Error('Capture ended during native processing');
        selected.bitmap = { format: 'compositor-element-canonical-srgb-rgba8-v2', width, height, tileSize, hashes };
        Object.assign(selected.bitmapMetrics, { nativeHashedBytes: hashedBytes, rehashedTiles, canonicalHashedBytes: hashedBytes, canonicalRehashedTiles: rehashedTiles, nativeRetainedRgbaBytes: width * height * 4, nativePeakRgbaBufferBytes: peakRgbaBytes });
      }
      if (entry.scopeProbe && selected.frame.captureProofBracket !== undefined) {
        const ordinal = selected.frame.captureProofBracket;
        if (!Number.isSafeInteger(ordinal) || ordinal !== entry.proofBracket + 1) throw Error('Capture diagnostic bracket changed');
        entry.proofBracket = ordinal;
        selected.frame.nativeScopeRequest = { frameId, loaderId, executionContextId: entry.contextId,
          captureId: entry.id, grantId: entry.grantId, grantRevision: capture.grantRevision, bracket: ordinal };
        delete selected.frame.captureProofBracket;
      }
      if (this.privateWorldPerCapture) {
        entry.frameSample = (entry.frameSample ?? 0) + 1;
        selected.frame.frameImportOrdinal = entry.frameSample;
      }
      const binding = selected.frame.binding;
      if (!binding?.viewportRect || selected.frame.surfaceId !== entry.id || selected.frame.sessionId !== entry.id) {
        throw Error('Restricted frame has no current geometry or surface identity');
      }
      // The digest identifies the coordinate space and surface across samples. Frame timestamps
      // and sample ordinals advance even when nothing in the rendered region changes.
      Object.assign(binding, { pageId, frameId, loaderId, selector: capture.selector, surfaceId: entry.id });
      Object.assign(selected.frame, { loaderId, grantId: capture.grantId, grantRevision: capture.grantRevision,
        bindingDigest: sha256Hex(canonicalJson(binding)) });
      return raw;
    } catch (error) { await this.end(entry); throw error; }
    finally { signal?.removeEventListener('abort', abort); }
  }
}
