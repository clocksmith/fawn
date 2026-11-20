// The private scripting world keeps the screen-sharing stream out of the page.
// The browser still presents its own chooser. Restrict the self-capture track
// before attaching any consumer, and stop it before returning a receipt.
import { createResidentCanvasReader } from './resident-canvas.js';
import { queryPage } from './page-query.js';

function createElementCapture(resident) {
  const surfaceIds = new WeakMap();
  return async function capture(element, { selector, origin, timeoutMs, verify, transport }) {
    if (location.origin !== origin || element !== document.querySelector(selector)
      || !(element instanceof HTMLCanvasElement) || !globalThis.RestrictionTarget?.fromElement
      || !globalThis.MediaStreamTrackProcessor) throw Error('Selected compositor surface unavailable');
    let active = true, stream, frame, reader, timer, phase = 'selecting element';
    const stop = () => {
      active = false;
      stream?.getTracks().forEach(track => track.stop());
    };
    const deadline = new Promise((_, reject) => {
      timer = setTimeout(() => { stop(); reject(Error('Compositor capture expired while ' + phase)); }, timeoutMs);
    });
    const take = async () => {
      if (!surfaceIds.has(element)) surfaceIds.set(element, crypto.randomUUID());
      const target = await RestrictionTarget.fromElement(element);
      if (!active) throw Error('Compositor capture expired');
      phase = 'waiting for tab permission';
      stream = await navigator.mediaDevices.getDisplayMedia({
        preferCurrentTab: true, surfaceSwitching: 'exclude', audio: false, video: { displaySurface: 'browser', cursor: 'never' },
      });
      const [track] = stream.getVideoTracks();
      if (!active || !track?.restrictTo || track.getSettings().displaySurface !== 'browser') {
        stop();
        throw Error('Capture requires the current browser tab');
      }
      phase = 'restricting compositor surface';
      await track.restrictTo(target);
      if (!active || !element.isConnected || element !== document.querySelector(selector)) throw Error('Selected surface changed');
      // Tab capture can suppress identical frames. Ask for a refresh cadence
      // after restriction so a static selection can issue a fresh observation.
      await track.applyConstraints({ frameRate: { min: 5, max: 15 } });
      reader = new MediaStreamTrackProcessor({ track, maxBufferSize: 1 }).readable.getReader();
      phase = 'waiting for the first restricted frame';
      const sample = await reader.read();
      if (sample.done) throw Error('Compositor capture ended');
      frame = sample.value;
      try {
        if (!active) throw Error('Compositor capture expired');
        phase = 'copying and hashing the restricted frame';
        const { width, height, tileSize, hashes, ...metrics } = await resident.readFrame(element, frame, verify, transport);
        if (!active || !element.isConnected || element !== document.querySelector(selector) || location.origin !== origin) {
          throw Error('Selected surface changed during capture');
        }
        return { bitmap: { format: 'compositor-element-srgb-rgba8-v1/' + transport, width, height, tileSize, hashes },
          bitmapMetrics: { path: 'element-capture-webgpu', ...metrics },
          frame: { source: 'element-capture', surfaceId: surfaceIds.get(element), transport, mediaTimestampUs: frame.timestamp, width, height } };
      } finally { frame.close(); }
    };
    addEventListener('pagehide', stop, { once: true });
    try { return await Promise.race([take(), deadline]); }
    finally {
      clearTimeout(timer);
      removeEventListener('pagehide', stop);
      stop();
      if (reader) await reader.cancel().catch(() => {});
    }
  };
}

/** Run queryPage in an isolated scripting world on the exact authorized page.
 * The scope is isolated per document, owner and grant; only GPU hashes/traces
 * survive a sample. The getDisplayMedia grant itself always ends per sample. */
export async function snapshotCompositor(send, args, capture) {
  const { frameTree } = await send('Page.getFrameTree', {});
  const { executionContextId } = await send('Page.createIsolatedWorld', {
    frameId: frameTree.frame.id, worldName: 'Blitzen selected compositor surface',
  });
  const scope = JSON.stringify([capture.origin, capture.ownerGeneration, capture.grantRevision, capture.readRoots, capture.selector, capture.transport]);
  const init = await send('Runtime.evaluate', { contextId: executionContextId, expression: `(() => {
    if (!globalThis.__blitzenSurface || globalThis.__blitzenSurface.scope !== ${JSON.stringify(scope)}) {
      globalThis.__blitzenSurface?.resident.dispose();
      const resident = (${createResidentCanvasReader.toString()})();
      globalThis.__blitzenSurface = { scope: ${JSON.stringify(scope)}, resident,
        capture: (${createElementCapture.toString()})(resident) };
    }
    return globalThis.__blitzenSurface;
  })()`, objectGroup: 'blitzen-surface-sample' });
  if (!init.result?.objectId) throw Error('Compositor capture context lost');
  try {
    const response = await send('Runtime.callFunctionOn', { objectId: init.result.objectId,
      functionDeclaration: `function(args, capture) { return (${queryPage.toString()})({ ...args,
        captureSelector: capture.selector, captureFrame: element => this.capture(element, capture) }); }`,
      arguments: [{ value: args }, { value: capture }], userGesture: true, awaitPromise: true, returnByValue: true });
    if (response.exceptionDetails) throw Error(response.exceptionDetails.exception?.description ?? response.exceptionDetails.text);
    const raw = response.result.value;
    const selected = raw?.regions.find(region => region.selector === capture.selector && region.frame);
    if (!selected) throw Error('Browser supplied no restricted compositor frame');
    Object.assign(selected.frame, { loaderId: frameTree.frame.loaderId, grantId: capture.grantId, grantRevision: capture.grantRevision });
    return raw;
  } finally { await send('Runtime.releaseObjectGroup', { objectGroup: 'blitzen-surface-sample' }); }
}
