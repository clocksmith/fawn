// Control implementation: the strongest reviewed click available to an
// ordinary MV3 extension. It screenshots the reviewed rectangle through the
// DevTools protocol, binds the element under the click point in its isolated
// content world, and before clicking re-screenshots, compares, re-checks the
// element and then dispatches. Those steps are separate asynchronous calls;
// an extension has no way to order them against the compositor or to make the
// final hit test part of the dispatch.
const reviews = new Map();
const attached = new Set();
const target = tabId => ({ tabId });

async function attach(tabId) {
  if (attached.has(tabId)) return;
  await chrome.debugger.attach(target(tabId), '1.3');
  attached.add(tabId);
}
// PNG bytes of the rectangle. Identical pixels encode identically in one browser.
async function snapshot(tabId, rect) {
  const { data } = await chrome.debugger.sendCommand(target(tabId), 'Page.captureScreenshot',
    { format: 'png', clip: { ...rect, scale: 1 }, captureBeyondViewport: false });
  return data;
}
const inPage = (tabId, func, args) => chrome.scripting.executeScript({ target: { tabId }, func, args })
  .then(([result]) => result.result);

self.activeTab = async () => (await chrome.tabs.query({ active: true, lastFocusedWindow: true }))[0].id;
self.prepare = tabId => attach(tabId);
self.review = async (tabId, rect, point) => {
  await attach(tabId);
  const image = await snapshot(tabId, rect);
  const bound = await inPage(tabId, (x, y) => {
    globalThis.__reviewed ??= new Map();
    const id = crypto.randomUUID();
    globalThis.__reviewed.set(id, document.elementFromPoint(x, y));
    return id;
  }, [point.x, point.y]);
  const id = crypto.randomUUID();
  reviews.set(id, { tabId, rect, point, image, bound });
  return { id, image };
};
// Qualification pause point between the final pixel check and the click, the
// counterpart of the browser's admission and delivery barriers. Inert unless
// the harness arms it.
self.armPause = () => {
  const pause = {};
  pause.reached = new Promise(resolve => { pause.reach = resolve; });
  pause.released = new Promise(resolve => { pause.release = resolve; });
  self.__pause = pause;
  self.__pauseHandle = pause;
  return true;
};
// The harness keeps its own handle: a click clears __pause when it stops there.
self.pauseReached = () => self.__pauseHandle.reached.then(() => true);
self.pauseRelease = () => { self.__pauseHandle.release(); return true; };

// mode 'trusted': re-check, then a trusted click through chrome.debugger.
// mode 'fenced': the same trusted click, with the content-script fence armed in
// every frame so it lands only on the bound element or is stopped.
// mode 'atomic': re-check the element and click it in one page task; the click
// is synthetic (isTrusted false), which pages may ignore.
self.click = async (id, mode) => {
  const review = reviews.get(id);
  if (!review) return { delivered: false, reason: 'unknown-review' };
  reviews.delete(id);
  const { tabId, rect, point, image, bound } = review;
  if (await snapshot(tabId, rect) !== image) return { delivered: false, reason: 'stale-pixels' };
  if (self.__pause) { const pause = self.__pause; self.__pause = null; pause.reach(); await pause.released; }
  if (mode === 'atomic') {
    const clicked = await inPage(tabId, (x, y, key) => {
      const element = globalThis.__reviewed?.get(key);
      if (!element || document.elementFromPoint(x, y) !== element) return false;
      element.click();
      return true;
    }, [point.x, point.y, bound]);
    return { delivered: clicked, reason: clicked ? 'delivered' : 'target-changed' };
  }
  const event = { x: point.x, y: point.y, button: 'left', clickCount: 1 };
  const press = async () => {
    await chrome.debugger.sendCommand(target(tabId), 'Input.dispatchMouseEvent', { type: 'mousePressed', ...event });
    await chrome.debugger.sendCommand(target(tabId), 'Input.dispatchMouseEvent', { type: 'mouseReleased', ...event });
  };
  if (mode === 'fenced') {
    const everyFrame = (func, args) => chrome.scripting.executeScript({ target: { tabId, allFrames: true }, func, args });
    await everyFrame(key => { globalThis.__fence = { element: globalThis.__reviewed?.get(key) ?? null, clicked: false, blocked: false }; }, [bound]);
    let seen;
    try { await press(); }
    finally {
      seen = await everyFrame(() => { const fence = globalThis.__fence; globalThis.__fence = null; return fence && { clicked: fence.clicked, blocked: fence.blocked }; }, []);
    }
    const clicked = seen.some(frame => frame.result?.clicked), blocked = seen.some(frame => frame.result?.blocked);
    return { delivered: clicked && !blocked, reason: clicked && !blocked ? 'delivered' : 'target-changed' };
  }
  const same = await inPage(tabId, (x, y, key) => document.elementFromPoint(x, y) === globalThis.__reviewed?.get(key),
    [point.x, point.y, bound]);
  if (!same) return { delivered: false, reason: 'target-changed' };
  await press();
  return { delivered: true, reason: 'delivered' };
};
