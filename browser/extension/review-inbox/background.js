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
let writing = Promise.resolve();
const update = (id, change) => (writing = writing.then(async () => {
  const items = await load();
  if (change === null) delete items[id]; else items[id] = { ...(items[id] ?? {}), ...change };
  await chrome.storage.session.set({ items });
  return items[id] ?? null;
}));

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
  if (!tab) return { holds: false, reason: 'The tab was closed.' };
  const probe = await inPage(item.tabId, (key, x, y, left, top) => {
    const element = globalThis.__fawnReviewed?.get(key) ?? null;
    // Scrolling the page since the review is not a change: put the control back where it was reviewed.
    if (element?.isConnected) {
      const r = element.getBoundingClientRect();
      if (Math.floor(r.x) !== left || Math.floor(r.y) !== top) window.scrollBy({ left: Math.floor(r.x) - left, top: Math.floor(r.y) - top, behavior: 'instant' });
    }
    return { bound: !!element, connected: !!element?.isConnected, sameUnderPoint: !!element && document.elementFromPoint(x, y) === element };
  }, [item.bound, item.point.x, item.point.y, item.rect.x, item.rect.y]).catch(() => null);
  if (!probe || probe.documentId !== item.documentId || !probe.result.bound) return { holds: false, reason: 'The page was reloaded or navigated.' };
  if (!probe.result.connected) return { holds: false, reason: 'The reviewed control is no longer on the page.' };
  if (!probe.result.sameUnderPoint) return { holds: false, reason: 'Something else now sits under the reviewed click point.' };
  const image = await currentImage(item);
  if (image === null) return { holds: false, reason: 'The browser could not capture the reviewed area.' };
  if (image !== item.image) return { holds: false, reason: 'The reviewed area changed after it was reviewed.', image };
  return { holds: true, reason: 'Matches what was reviewed.' };
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
  return update(id, { id, tabId, windowId: tab.windowId, documentId, url: picked.url, pageTitle: picked.pageTitle, rect: picked.rect, point: picked.point,
    bound: picked.key, image, summary: picked.summary, state: 'pending', createdAt: now(), check: { at: now(), holds: true, reason: 'Matches what was reviewed.' } });
}

async function check(id) {
  const item = (await load())[id];
  if (!item || !['pending', 'approved'].includes(item.state)) return item ?? null;
  const status = await stillHolds(item);
  return update(id, { check: { at: now(), holds: status.holds, reason: status.reason, ...(status.image ? { image: status.image } : {}) } });
}

async function approve(id) {
  const item = (await load())[id];
  if (!item || item.state !== 'pending') throw new Error('This item is not waiting for approval.');
  const status = await stillHolds(item);
  const seen = { at: now(), holds: status.holds, reason: status.reason, ...(status.image ? { image: status.image } : {}) };
  if (!status.holds) { await update(id, { check: seen }); throw new Error(`${status.reason} Review it again before approving.`); }
  return update(id, { state: 'approved', approvedAt: now(), check: seen });
}

async function finish(id) {
  const item = (await load())[id];
  if (!item || item.state !== 'approved') throw new Error('Approve this item before finishing it.');
  // Closed from here on: whatever happens, this approval is spent.
  await update(id, { state: 'finishing' });
  const settle = (state, text, detail = '') => update(id, { state, outcome: { text, detail, at: now() } });
  const recipe = RECIPES[item.summary.recipe] ?? null;
  const noun = recipe?.noun ?? 'result';
  try {
    const tab = await chrome.tabs.get(item.tabId).catch(() => null);
    if (!tab) return settle('blocked', 'Click blocked', 'The tab was closed.');
    {
      const status = await stillHolds(item);
      if (!status.holds) {
        await update(id, { check: { at: now(), holds: false, reason: status.reason, ...(status.image ? { image: status.image } : {}) } });
        return settle('blocked', 'Click blocked', status.reason);
      }
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
      if (blocked || (!clicked && !navigated)) return settle('blocked', 'Click blocked', 'The click did not land on the reviewed control, so it was stopped.');
      if (!recipe || !before) return settle('unconfirmed', `Click sent; ${noun} not confirmed`, 'This kind of control has no known sign of its result.');
      // Only what is new counts: a sign that was already there is this click's result only if the page
      // reloaded or more of them appeared.
      for (const deadline = Date.now() + RESULT_WINDOW_MS; ;) {
        const state = await resultSoon(item.tabId, recipe); // null while the page is navigating
        if (state) {
          const fresh = key => state[key] > 0 && (before[key] === 0 || state.document !== before.document || state[key] > before[key]);
          if (fresh('refused')) return settle('refused', 'App refused', state.said);
          if (fresh('done')) return settle('confirmed', recipe.doneLabel);
        }
        if (Date.now() >= deadline) return settle('unconfirmed', `Click sent; ${noun} not confirmed`, 'The application showed neither success nor refusal. Check the page; nothing will be clicked again.');
        await new Promise(resolve => setTimeout(resolve, RESULT_POLL_MS));
      }
    }
  } catch (error) {
    return settle('unconfirmed', `Click may have been sent; ${noun} not confirmed`, `The inbox lost track of this click: ${error.message}`);
  }
}

const operations = {
  tabs: async () => (await chrome.tabs.query({})).filter(tab => /^https?:/.test(tab.url ?? ''))
    .map(tab => ({ id: tab.id, title: tab.title, url: tab.url, active: tab.active, windowId: tab.windowId })),
  items: async () => Object.values(await load()).sort((a, b) => a.createdAt.localeCompare(b.createdAt)),
  pick: message => pick(message.tabId),
  check: message => check(message.id),
  approve: message => approve(message.id),
  finish: message => finish(message.id),
  remove: message => update(message.id, null),
};
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  const operation = operations[message?.inbox];
  if (!operation) return false;
  Promise.resolve().then(() => operation(message)).then(value => respond({ ok: true, value }), error => respond({ ok: false, error: error.message }));
  return true;
});
// A tab that closes takes its pending items with it: nothing can be clicked there any more.
chrome.tabs.onRemoved.addListener(async tabId => {
  for (const item of Object.values(await load())) {
    if (item.tabId === tabId && ['pending', 'approved'].includes(item.state)) await update(item.id, { check: { at: now(), holds: false, reason: 'The tab was closed.' } });
  }
});
chrome.sidePanel?.setPanelBehavior?.({ openPanelOnActionClick: true }).catch(() => {});
