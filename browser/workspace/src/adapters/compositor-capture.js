// Browser-only closure. Only the isolated world owns the MediaStream or frames.
import { randomUUID } from 'node:crypto';
import { createResidentCanvasReader } from './resident-canvas.js';
import { queryPage } from './page-query.js';

function createElementCapture(id, resident, capture, leaseMs) {
  const { selector, origin } = capture;
  let element, stream, track, reader, observer, watchdog, expiry, ended = false;
  const stop = () => {
    if (ended) return;
    ended = true;
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
    let timer;
    async function read() {
      if (newStream) await start(selected);
      if (element !== selected) throw Error('The selected element was replaced');
      assertCurrent();
      // The processor can hold a frame from before this observation. Discard
      // the queued frame and consume the next restricted compositor update.
      let closedFrames = 0;
      if (!newStream) {
        const queued = await reader.read();
        if (queued.done) throw Error('Sharing ended');
        queued.value.close(); closedFrames++;
      }
      const next = await reader.read();
      if (next.done) throw Error('Sharing ended');
      const frame = next.value;
      try {
        assertCurrent();
        const { width, height, tileSize, hashes, ...metrics } = await resident.readFrame(element, frame, verify, capture.transport);
        assertCurrent();
        return { bitmap: { format: 'compositor-element-srgb-rgba8-v1/' + capture.transport, width, height, tileSize, hashes },
          bitmapMetrics: { path: 'element-capture-webgpu', ...metrics, permissionRequests: newStream ? permissionRequests : 0,
            consumedFrames: closedFrames + 1, discardedFrames: closedFrames },
          frame: { source: 'element-capture', surfaceId: id, sessionId: id, transport: capture.transport,
            mediaTimestampUs: frame.timestamp, width, height } };
      } finally { frame.close(); }
    }
    try {
      if (ended) throw Error('Sharing ended; authorize a new capture');
      return await Promise.race([read(), new Promise((_, reject) => {
        timer = setTimeout(() => { stop(); reject(Error('Timed out waiting for restricted compositor evidence')); }, timeoutMs);
      })]);
    } catch (error) { stop(); throw error; } finally { clearTimeout(timer); }
  }
  return { id, sample, renew, stop, status: () => ({ ended, trackState: track?.readyState, permissionRequests }) };
}

// One restricted capture per workspace page. The live authority is deliberately
// absent from disk. Replaying an old id after a restart cannot open a chooser.
export class CompositorSessions {
  entries = new Map();

  async end(entry) {
    if (this.entries.get(entry.key) !== entry) return;
    this.entries.delete(entry.key); clearInterval(entry.heartbeat);
    if (entry.contextId === undefined) return;
    let timer;
    try {
      // Handoff and shutdown must finish even if the browser pipe is wedged.
      // With no renewals the renderer's own lease also retires the track.
      await Promise.race([this.control(entry, 'stop'), new Promise(resolve => { timer = setTimeout(resolve, 1000); })]);
    } catch { /* A closed frame has already relinquished its native capture. */ }
    finally { clearTimeout(timer); }
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
      capture.readRoots, capture.selector, capture.transport]);
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
      const { executionContextId } = await send('Page.createIsolatedWorld', { frameId, worldName: 'Blitzen selected compositor surface' });
      signal?.throwIfAborted();
      entry = { key, workspaceId, grantId: capture.grantId, scope, id: randomUUID(), send, contextId: executionContextId };
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
            (${createResidentCanvasReader.toString()})(), ${JSON.stringify(capture)}, ${leaseMs}); return true;
        })()` });
        if (signal?.aborted || this.entries.get(key) !== entry) {
          await this.control(entry, 'stop').catch(() => {});
          throw Error('Capture request ended during initialization');
        }
        if (init.exceptionDetails) throw Error('Could not initialize restricted surface capture');
        let pending = false;
        entry.heartbeat = setInterval(async () => {
          if (pending) return;
          pending = true;
          try {
            const reply = await this.control(entry, 'renew');
            if (reply.result?.value !== true) await this.end(entry);
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
      Object.assign(selected.frame, { loaderId, grantId: capture.grantId, grantRevision: capture.grantRevision });
      return raw;
    } catch (error) { await this.end(entry); throw error; }
    finally { signal?.removeEventListener('abort', abort); }
  }
}
