// Fawn review inbox, as an ordinary extension.
//
// An item is one approval: a control a person picked on a page, the pixels they
// saw there, and the tab, document and element it belongs to. Approving covers
// exactly those pixels. Finishing clicks only that element in that document,
// after checking that the pixels still match, and then reports one of:
//   Click blocked            nothing was sent; the reason says what changed
//   App refused              the click was sent and the application said no
//   <done label>             the click was sent and the application shows it done
//   Click sent; not confirmed  the click was sent and neither sign appeared
// A finished item is closed. It is never clicked again; a person reviews again.
//
// The checks are separate asynchronous steps. An extension cannot order them
// against the compositor or make the final hit test part of the dispatch; the
// fence in page.js stops a trusted click that lands on anything but the bound
// element, which is the strongest this kind of browser extension can offer.
//
// An item's states, the moves between them and the words for each reason are
// defined once, in lifecycle.js. Here every change of state goes through `move`.
import { STATE, REMOVED, REASON, OUTCOME, isOpen, isTerminal, refusal, whenTabCloses, whenDocumentIs } from './lifecycle.js';

const RESULT_WINDOW_MS = 10000, RESULT_POLL_MS = 200, CAPTURE_TIMEOUT_MS = 2500;
// How an application shows the result of a control, per kind of control found by page.js.
const RECIPES = {
  'forgejo-merge': { noun: 'merge', done: '.issue-state-label.purple', doneLabel: 'Merge confirmed',
    refused: '.flash-error, .toastify.on' },
};
const target = tabId => ({ tabId });
// A page that is navigating may not answer a script injection for a long time. Nothing after the click waits on one.
const within = (promise, ms, fallback) => Promise.race([promise, new Promise(resolve => setTimeout(() => resolve(fallback), ms))]);
const now = () => new Date().toISOString();

// Items live in session storage: they outlive this worker's restarts and end with the browser session,
// as the pages and elements they are bound to do.
const load = async () => (await chrome.storage.session.get('items')).items ?? {};
// Everything that reads the items in order to change something runs on one chain, one step at a time.
let writing = Promise.resolve();
const inTurn = work => { const done = writing.then(work); writing = done.catch(() => {}); return done; };
// `decide` sees the stored item and answers with what it becomes: an item, null to discard it, or undefined
// to leave it as it is.
const write = (id, decide) => inTurn(async () => {
  const items = await load(), before = items[id], after = decide(before);
  if (after === undefined) return before ?? null;
  if (after === null) delete items[id]; else items[id] = after;
  await chrome.storage.session.set({ items });
  return after;
});
// The one way an item changes state, discarding it included. `next` is the change, state and all, or a
// function from the stored item to the change (null: leave it). The table in lifecycle.js decides: a move
// it does not allow throws its reason and writes nothing. Because the decision is made on the stored item,
// of any number of overlapping requests for the same move exactly one gets it. An item that is not there
// stays not there; only `pick` makes one.
async function move(id, next) {
  let left = null;
  const item = await write(id, before => {
    const change = before && (typeof next === 'function' ? next(before) : next);
    if (!change) return undefined;
    const reason = refusal(before.state, change.state);
    if (reason) throw new Error(reason);
    if (change.state === REMOVED || isTerminal(change.state)) left = before;
    return change.state === REMOVED ? null : { ...before, ...change };
  });
  if (left) release(left);
  return item;
}
// A fresh look at an open item. It changes no state, and is dropped if the item has closed or gone meanwhile.
const note = (id, check) => write(id, before => before && isOpen(before.state) ? { ...before, check } : undefined);
const checked = status => ({ at: now(), holds: status.holds, reason: status.reason, ...(status.image ? { image: status.image } : {}) });

// The debugger lets go of a tab once none of its items is unfinished and no pick is under way there (a pick
// holds the debugger before its item exists). Asked in turn with the writes, so it sees every item written
// before it and none half-written.
const picking = new Set();
const letGo = tabId => inTurn(async () => {
  const unfinished = Object.values(await load()).some(item => item.tabId === tabId && !isTerminal(item.state));
  if (!unfinished && !picking.has(tabId)) chrome.debugger.detach(target(tabId)).catch(() => {});
});
// One teardown for every item that closes or is discarded. The page lets go of the element it held for the
// item, and the debugger lets go of the tab if nothing there still needs it. Nothing waits on the page: it
// may be gone, or busy navigating.
function release(item) {
  inPage(item.tabId, key => { globalThis.__fawnReviewed?.delete(key); }, [item.bound]).catch(() => {});
  letGo(item.tabId).catch(() => {});
}
async function whilePicking(tabId, work) {
  picking.add(tabId);
  try { return await work(); }
  finally { picking.delete(tabId); letGo(tabId).catch(() => {}); }
}
// A worker that stops in the middle of a finish takes the rest of that finish with it. The next worker
// cannot know whether the click went out, so it says so and leaves the item spent.
const WORKER = crypto.randomUUID();
const recovered = load().then(items => Promise.all(Object.values(items).map(item => move(item.id, stored =>
  stored.state === STATE.FINISHING && stored.finishingBy !== WORKER ? { state: STATE.UNCONFIRMED, outcome: { at: now(),
    detail: REASON.workerStopped, text: OUTCOME.maybeSent(RECIPES[stored.summary?.recipe]?.noun ?? 'result') } } : null))));

async function attach(tabId) {
  try { await chrome.debugger.attach(target(tabId), '1.3'); }
  catch (error) { if (!/already attached/i.test(error.message)) throw error; }
}
const inPage = (tabId, func, args = []) => chrome.scripting.executeScript({ target: { tabId, frameIds: [0] }, func, args })
  .then(([result]) => result);
const everyFrame = (tabId, func, args = []) => chrome.scripting.executeScript({ target: { tabId, allFrames: true }, func, args });

// PNG bytes of the rectangle, which is given in viewport coordinates. The debugger's clip is in page
// coordinates, so the page's current scroll offset is added. Identical pixels encode identically in one browser.
async function capture(tabId, rect) {
  // A button looks different with the pointer over it. Every capture is taken with the pointer parked in
  // the page's corner, so the reviewed image and every later comparison show the same resting state.
  await chrome.debugger.sendCommand(target(tabId), 'Input.dispatchMouseEvent', { type: 'mouseMoved', x: 0, y: 0 });
  const scroll = (await inPage(tabId, () => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => resolve(
    { x: window.visualViewport?.pageLeft ?? window.scrollX, y: window.visualViewport?.pageTop ?? window.scrollY })))))).result;
  const shot = chrome.debugger.sendCommand(target(tabId), 'Page.captureScreenshot',
    { format: 'png', clip: { x: rect.x + scroll.x, y: rect.y + scroll.y, width: rect.width, height: rect.height, scale: 1 }, captureBeyondViewport: false })
    .then(result => result.data);
  return Promise.race([shot, new Promise((_, reject) => setTimeout(() => reject(new Error('capture-timeout')), CAPTURE_TIMEOUT_MS))]);
}
// A tab behind another may not draw. Bring it forward for as long as `work` needs its pixels, then put back
// the tab the person was looking at.
async function withVisibleTab(tabId, work) {
  const tab = await chrome.tabs.get(tabId);
  if (tab.active) return work();
  const [front] = await chrome.tabs.query({ active: true, windowId: tab.windowId });
  await chrome.tabs.update(tabId, { active: true });
  await new Promise(resolve => setTimeout(resolve, 150));
  try { return await work(); }
  finally { if (front && front.id !== tabId) await chrome.tabs.update(front.id, { active: true }).catch(() => {}); }
}

// The reviewed rectangle as the tab shows it now. A tab behind another is captured where it is when the
// browser allows it and the pixels match, so finishing an item does not pull its tab in front of the person;
// otherwise the tab is brought forward for the capture and the earlier tab is put back.
async function currentImage(item) {
  const tab = await chrome.tabs.get(item.tabId);
  if (!tab.active) {
    const behind = await capture(item.tabId, item.rect).catch(() => null);
    if (behind === item.image) return behind;
  }
  return withVisibleTab(item.tabId, () => capture(item.tabId, item.rect)).catch(() => null);
}

// Does the item's tab still show the document, element and pixels that were reviewed?
async function stillHolds(item) {
  const tab = await chrome.tabs.get(item.tabId).catch(() => null);
  if (!tab) return { holds: false, reason: REASON.tabClosed };
  const probe = await inPage(item.tabId, (key, x, y, left, top) => {
    const element = globalThis.__fawnReviewed?.get(key) ?? null;
    // Scrolling the page since the review is not a change: put the control back where it was reviewed.
    if (element?.isConnected) {
      const r = element.getBoundingClientRect();
      if (Math.floor(r.x) !== left || Math.floor(r.y) !== top) window.scrollBy({ left: Math.floor(r.x) - left, top: Math.floor(r.y) - top, behavior: 'instant' });
    }
    return { bound: !!element, connected: !!element?.isConnected, sameUnderPoint: !!element && document.elementFromPoint(x, y) === element };
  }, [item.bound, item.point.x, item.point.y, item.rect.x, item.rect.y]).catch(() => null);
  if (!probe || probe.documentId !== item.documentId || !probe.result.bound) return { holds: false, reason: REASON.documentReplaced };
  if (!probe.result.connected) return { holds: false, reason: REASON.controlGone };
  if (!probe.result.sameUnderPoint) return { holds: false, reason: REASON.covered };
  const image = await currentImage(item);
  if (image === null) return { holds: false, reason: REASON.noCapture };
  if (image !== item.image) return { holds: false, reason: REASON.pixelsChanged, image };
  return { holds: true, reason: REASON.matches };
}

// What the application currently shows of this control's result, and in which document.
const resultState = (tabId, recipe) => inPage(tabId, (done, refused) => {
  const visible = [...document.querySelectorAll(refused)].filter(element => element.getClientRects().length);
  return { document: String(performance.timeOrigin), done: document.querySelectorAll(done).length, refused: document.querySelectorAll(refused).length,
    said: visible.map(element => element.textContent.trim().replace(/\s+/g, ' ').slice(0, 200)).join(' | ') };
}, [recipe.done, recipe.refused]).then(result => result.result, () => null);
const resultSoon = (tabId, recipe) => within(resultState(tabId, recipe), 1000, null);

async function pick(tabId) {
  const tab = await chrome.tabs.get(tabId);
  await chrome.tabs.update(tabId, { active: true });
  await chrome.windows.update(tab.windowId, { focused: true }).catch(() => {});
  // Attaching shows the debugging bar, which moves the page; it must be there before anything is measured.
  await attach(tabId);
  await new Promise(resolve => setTimeout(resolve, 800));
  const picked = await chrome.tabs.sendMessage(tabId, { fawn: 'pick' }, { frameId: 0 });
  if (!picked || picked.cancelled) return null;
  const { documentId } = await inPage(tabId, () => true);
  const image = await capture(tabId, picked.rect);
  const id = crypto.randomUUID();
  return write(id, () => ({ id, tabId, windowId: tab.windowId, documentId, url: picked.url, pageTitle: picked.pageTitle, rect: picked.rect, point: picked.point,
    bound: picked.key, image, summary: picked.summary, state: STATE.PENDING, createdAt: now(), check: { at: now(), holds: true, reason: REASON.matches } }));
}

async function check(id) {
  const item = (await load())[id];
  if (!item || !isOpen(item.state)) return item ?? null;
  return note(id, checked(await stillHolds(item)));
}

async function approve(id) {
  const item = (await load())[id];
  if (!item || item.state !== STATE.PENDING) throw new Error(REASON.notPending);
  const status = await stillHolds(item), check = checked(status);
  if (!status.holds) { await note(id, check); throw new Error(REASON.reviewAgain(status.reason)); }
  return move(id, { state: STATE.APPROVED, approvedAt: now(), check });
}

async function finish(id) {
  await recovered;
  // Closed from here on: whatever happens, this approval is spent, and only the request that spent it clicks.
  const item = await move(id, { state: STATE.FINISHING, finishingBy: WORKER });
  if (!item) throw new Error(REASON.notApproved);
  // If the item has gone by the time its result is known, the result is dropped: `move` does not bring it back.
  const settle = (state, text, detail = '', more = {}) => move(id, { ...more, state, outcome: { text, detail, at: now() } });
  const recipe = RECIPES[item.summary.recipe] ?? null;
  const noun = recipe?.noun ?? 'result';
  try {
    const tab = await chrome.tabs.get(item.tabId).catch(() => null);
    if (!tab) return settle(STATE.BLOCKED, OUTCOME.blocked, REASON.tabClosed);
    {
      const status = await stillHolds(item);
      if (!status.holds) return settle(STATE.BLOCKED, OUTCOME.blocked, status.reason, { check: checked(status) });
      const before = recipe ? await resultState(item.tabId, recipe) : null;
      // Arm the fence in every frame, click through the debugger, then read what the fence saw.
      await everyFrame(item.tabId, key => { globalThis.__fawnFence = { element: globalThis.__fawnReviewed?.get(key) ?? null, clicked: false, blocked: false }; }, [item.bound]);
      const event = { x: item.point.x, y: item.point.y, button: 'left', clickCount: 1 };
      let seen = [];
      try {
        // The page may start navigating as the click lands, and then the browser can hold back its
        // acknowledgement of the event for a long time. The event has been sent either way.
        const send = type => within(chrome.debugger.sendCommand(target(item.tabId), 'Input.dispatchMouseEvent', { type, ...event }).catch(() => null), 1500, null);
        await send('mousePressed');
        await send('mouseReleased');
      } finally {
        seen = await within(everyFrame(item.tabId, () => { const fence = globalThis.__fawnFence; globalThis.__fawnFence = null;
          return fence && { clicked: fence.clicked, blocked: fence.blocked }; }).catch(() => []), 1000, []);
      }
      const clicked = seen.some(frame => frame.result?.clicked), blocked = seen.some(frame => frame.result?.blocked);
      // A page that navigated at once leaves no fence to read; the click was sent to the bound element's document.
      const navigated = seen.length === 0 || seen.every(frame => !frame.result);
      if (blocked || (!clicked && !navigated)) return settle(STATE.BLOCKED, OUTCOME.blocked, REASON.fenced);
      if (!recipe || !before) return settle(STATE.UNCONFIRMED, OUTCOME.notConfirmed(noun), REASON.noKnownSign);
      // Only what is new counts: a sign that was already there is this click's result only if the page
      // reloaded or more of them appeared.
      for (const deadline = Date.now() + RESULT_WINDOW_MS; ;) {
        const state = await resultSoon(item.tabId, recipe); // null while the page is navigating
        if (state) {
          const fresh = key => state[key] > 0 && (before[key] === 0 || state.document !== before.document || state[key] > before[key]);
          if (fresh('refused')) return settle(STATE.REFUSED, OUTCOME.refused, state.said);
          if (fresh('done')) return settle(STATE.CONFIRMED, recipe.doneLabel);
        }
        if (Date.now() >= deadline) return settle(STATE.UNCONFIRMED, OUTCOME.notConfirmed(noun), REASON.neitherSign);
        await new Promise(resolve => setTimeout(resolve, RESULT_POLL_MS));
      }
    }
  } catch (error) {
    return settle(STATE.UNCONFIRMED, OUTCOME.maybeSent(noun), REASON.lostTrack(error.message));
  }
}

const operations = {
  tabs: async () => (await chrome.tabs.query({})).filter(tab => /^https?:/.test(tab.url ?? ''))
    .map(tab => ({ id: tab.id, title: tab.title, url: tab.url, active: tab.active, windowId: tab.windowId })),
  items: async () => Object.values((await recovered, await load())).sort((a, b) => a.createdAt.localeCompare(b.createdAt)),
  pick: message => whilePicking(message.tabId, () => pick(message.tabId)),
  check: message => check(message.id),
  approve: message => approve(message.id),
  finish: message => finish(message.id),
  remove: message => move(message.id, { state: REMOVED }),
};
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  const operation = operations[message?.inbox];
  if (!operation) return false;
  Promise.resolve().then(() => operation(message)).then(value => respond({ ok: true, value }), error => respond({ ok: false, error: error.message }));
  return true;
});
// A tab that closes, or whose top document is replaced, takes its open items with it: nothing reviewed is
// left there to click, so they close as blocked and say why. An item that is being finished is left to its finish.
async function closeOpen(tabId, decide) {
  for (const item of Object.values(await load())) if (item.tabId === tabId && decide(item)) await move(item.id, decide).catch(() => null);
}
chrome.tabs.onRemoved.addListener(tabId => closeOpen(tabId, item => whenTabCloses(item, now())));
// No tab event says "this is a new document", so on any change to a tab that has open items the tab is
// asked which document it shows. A tab that does not answer is left for the checks before a click.
chrome.tabs.onUpdated.addListener(async tabId => {
  if (!Object.values(await load()).some(item => item.tabId === tabId && isOpen(item.state))) return;
  const shown = await within(chrome.scripting.executeScript({ target: { tabId, frameIds: [0] }, func: () => true, injectImmediately: true })
    .then(([frame]) => frame?.documentId ?? null, () => null), 1000, null);
  if (shown) await closeOpen(tabId, item => whenDocumentIs(item, shown, now()));
});
chrome.sidePanel?.setPanelBehavior?.({ openPanelOnActionClick: true }).catch(() => {});

// For the tests, which load this worker against a stand-in for `chrome`.
export { move };
