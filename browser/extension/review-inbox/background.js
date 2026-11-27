// Fawn review inbox, as an ordinary extension.
//
// An item is one approval: a control a person picked on a page, the pixels they
// saw there, and the tab, document and element it belongs to. Approving covers
// exactly those pixels. Finishing clicks only that element in that document,
// after checking that the pixels still match, and then reports one of:
//   Click blocked            nothing was sent; the reason says what changed
//   App refused              the click was sent and the application said no
//   <done label>             the click was sent and the application shows it done
//   Click sent; not confirmed  the click was sent and neither sign appeared, or its request got no answer
// A finished item is never clicked again. One whose result is not known stays so, and blocks every Finish,
// until a person reconciles it: a read of the application's own record, which writes nothing and ends as
// confirmed, not applied (a person may then approve exactly this again) or still not known.
//
// The checks are separate asynchronous steps. An extension cannot order them
// against the compositor or make the final hit test part of the dispatch; the
// fence in page.js stops a trusted click that lands on anything but the bound
// element, which is the strongest this kind of browser extension can offer.
//
// An item's states, the moves between them and the words for each reason are
// defined once, in lifecycle.js. Here every change of state goes through `move`.
import { STATE, REMOVED, REASON, OUTCOME, REREAD_MS, isOpen, isTerminal, isCheckable, refusal, reconciled, whenTabCloses, whenDocumentIs } from './lifecycle.js';

// Every wait on a page is bounded. A page that does not answer in time fails closed: it is not clicked.
const RESULT_WINDOW_MS = 10000, RESULT_POLL_MS = 200, CAPTURE_TIMEOUT_MS = 2500, PAGE_TIMEOUT_MS = 2500, RECORD_TIMEOUT_MS = 8000;
const SILENT = Symbol('silent');
const short = sha => sha ? sha.slice(0, 10) : 'none';
// How an application shows the result of a control, per kind of control found by page.js. `done` and
// `refused` are what the page shows. `record` and `view` are the application's own record of the target, for
// a control that has one: `record` gets the target page.js read when the control was picked and a reader that
// can only GET, and `view` measures what it returns against the approved terms.
const RECIPES = {
  'forgejo-merge': { noun: 'merge', done: '.issue-state-label.purple', doneLabel: 'Merge confirmed',
    refused: '.flash-error, .toastify.on',
    record: (target, read) => read(`${target.base}/api/v1/repos/${target.repo}/pulls/${target.number}`),
    // The approval is for one pull request, open and unmerged, at the head commit that was reviewed. The
    // merge it sends names that commit, so only a merge of it is this approval's.
    view: (target, pull) => {
      const head = pull.head?.sha ?? null, number = `#${target.number}`, changes = [];
      const same = pull.number === target.number && String(pull.base?.repo?.full_name).toLowerCase() === target.repo.toLowerCase();
      if (!same) changes.push(`Forgejo answered for ${pull.base?.repo?.full_name ?? '?'}#${pull.number}, not ${target.repo}${number}`);
      if (head !== target.head) changes.push(`pull request ${number} now has head commit ${short(head)}; the reviewed one was ${short(target.head)}`);
      if (pull.merged) changes.push(`pull request ${number} is already merged`);
      else if (pull.state !== 'open') changes.push(`pull request ${number} is ${pull.state}`);
      return { applied: same && pull.merged === true && head === target.head, changes,
        evidence: pull.merged ? `pull request ${number} merged${pull.merged_by?.login ? ' by ' + pull.merged_by.login : ''} as commit ${short(pull.merge_commit_sha)} from head ${short(head)}`
          : `pull request ${number} is ${pull.state} and not merged, at head ${short(head)}`,
        signature: JSON.stringify([pull.state, !!pull.merged, head, pull.merge_commit_sha ?? null]) };
    } },
};
const target = tabId => ({ tabId });
// A page that is navigating may not answer a script injection for a long time. Nothing after the click waits on one.
const within = (promise, ms, fallback) => Promise.race([promise, new Promise(resolve => setTimeout(() => resolve(fallback), ms))]);
const now = () => new Date().toISOString();

// Items live in local storage, which outlives this worker's restarts and the browser's: an item whose click
// may have gone out must still be there, unknown, after a crash. The pages and elements items are bound to end
// with the browser session; session storage holds a mark of the session, so a new worker can tell a worker
// restart from a browser restart.
const load = async () => (await chrome.storage.local.get('items')).items ?? {};
// Everything that reads the items in order to change something runs on one chain, one step at a time.
let writing = Promise.resolve();
const inTurn = work => { const done = writing.then(work); writing = done.catch(() => {}); return done; };
// `decide` sees the stored item and answers with what it becomes: an item, null to discard it, or undefined
// to leave it as it is.
const write = (id, decide) => inTurn(async () => {
  const items = await load(), before = items[id], after = decide(before);
  if (after === undefined) return before ?? null;
  if (after === null) delete items[id]; else items[id] = after;
  await chrome.storage.local.set({ items });
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
// A fresh look at an item that may still be approved or finished. It changes no state, and is dropped if the
// item has moved on or gone meanwhile.
const note = (id, check) => write(id, before => before && isCheckable(before.state) ? { ...before, check } : undefined);
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
  if (item.tabId == null) return; // its tab went with an earlier browser session
  inPage(item.tabId, key => { globalThis.__fawnReviewed?.delete(key); }, [item.bound]).catch(() => {});
  letGo(item.tabId).catch(() => {});
}
async function whilePicking(tabId, work) {
  picking.add(tabId);
  try { return await work(); }
  finally { picking.delete(tabId); letGo(tabId).catch(() => {}); }
}
// A worker that stops in the middle of a finish takes the rest of that finish with it. The next worker
// cannot know whether the click went out, so the result is not known until someone reconciles it. A worker
// that stops while reading a record wrote nothing, and the result is as unknown as before. After a browser
// restart the same holds, and in addition every tab, document and element an item was bound to is gone: an
// open item closes as blocked, and every item forgets its tab, whose id may now belong to another page.
const WORKER = crypto.randomUUID();
const nounOf = item => RECIPES[item.summary?.recipe]?.noun ?? 'result';
function carriedOver(stored, restarted) {
  const unbound = restarted ? { tabId: null } : {};
  if (stored.state === STATE.FINISHING && stored.finishingBy !== WORKER) return { ...unbound, state: STATE.UNKNOWN,
    outcome: { at: now(), detail: restarted ? REASON.browserStopped : REASON.workerStopped, text: OUTCOME.maybeSent(nounOf(stored)) } };
  if (stored.state === STATE.RECONCILING && stored.reconcilingBy !== WORKER) return { ...unbound, state: STATE.UNKNOWN,
    outcome: { at: now(), detail: REASON.reconcileStopped, text: OUTCOME.stillUnknown(nounOf(stored)) } };
  if (restarted && isOpen(stored.state)) return { ...unbound, state: STATE.BLOCKED, check: { at: now(), holds: false, reason: REASON.browserRestarted },
    outcome: { at: now(), text: OUTCOME.blocked, detail: REASON.browserRestarted } };
  return null;
}
const recovered = (async () => {
  const restarted = !(await chrome.storage.session.get('browserSession')).browserSession;
  for (const item of Object.values(await load())) {
    await move(item.id, stored => carriedOver(stored, restarted)).catch(() => null);
    // Not a change of state: the item keeps its result and only lets go of a tab that is no longer its own.
    if (restarted) await write(item.id, stored => stored && stored.tabId !== null ? { ...stored, tabId: null } : undefined);
  }
  // Marked only once every item is carried over, so a worker that stops halfway starts over.
  if (restarted) await chrome.storage.session.set({ browserSession: now() });
})();

async function attach(tabId) {
  try { await chrome.debugger.attach(target(tabId), '1.3'); }
  catch (error) { if (!/already attached/i.test(error.message)) throw error; }
}
const inPage = (tabId, func, args = []) => chrome.scripting.executeScript({ target: { tabId, frameIds: [0] }, func, args })
  .then(([result]) => result);
const everyFrame = (tabId, func, args = []) => chrome.scripting.executeScript({ target: { tabId, allFrames: true }, func, args });

// PNG bytes of the rectangle, which is given in viewport coordinates. The debugger's clip is in page
// coordinates, so the page's current scroll offset is added. Identical pixels encode identically in one browser.
// The whole capture is bounded, the wait for the page's next frames included: a tab in the background may
// draw no frames at all.
function capture(tabId, rect) {
  const shot = (async () => {
    // A button looks different with the pointer over it. Every capture is taken with the pointer parked in
    // the page's corner, so the reviewed image and every later comparison show the same resting state.
    await chrome.debugger.sendCommand(target(tabId), 'Input.dispatchMouseEvent', { type: 'mouseMoved', x: 0, y: 0 });
    const scroll = (await inPage(tabId, () => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => resolve(
      { x: window.visualViewport?.pageLeft ?? window.scrollX, y: window.visualViewport?.pageTop ?? window.scrollY })))))).result;
    return (await chrome.debugger.sendCommand(target(tabId), 'Page.captureScreenshot',
      { format: 'png', clip: { x: rect.x + scroll.x, y: rect.y + scroll.y, width: rect.width, height: rect.height, scale: 1 }, captureBeyondViewport: false })).data;
  })();
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

// The application's own record of an item's target, read without writing anything. `read` is the only way
// a recipe's record reaches the application: a GET for JSON on the reviewed page's own origin, with no method,
// body or header the caller can choose. Nothing that goes through it is a click or a write.
function reader(item) {
  const origin = new URL(item.url).origin;
  return async address => {
    const url = new URL(address);
    if (url.origin !== origin) throw new Error(`${url.origin} is not the reviewed page's origin`);
    const response = await fetch(url, { method: 'GET', credentials: 'include', cache: 'no-store', redirect: 'error',
      headers: { accept: 'application/json' }, signal: AbortSignal.timeout(RECORD_TIMEOUT_MS) });
    if (!response.ok) throw new Error(`${url.pathname} answered HTTP ${response.status}`);
    return response.json();
  };
}
// What the record shows now, measured against the approved terms; null when the control has no record.
async function viewRecord(item) {
  const recipe = RECIPES[item.summary?.recipe], target = item.summary?.target;
  if (!recipe?.record || !target) return null;
  return recipe.view(target, await recipe.record(target, reader(item)));
}

// Does the item's tab still show the document, element and pixels that were reviewed, and where the
// application keeps a record of the target, does it still hold the target and values that were approved?
async function stillHolds(item) {
  const tab = item.tabId == null ? null : await chrome.tabs.get(item.tabId).catch(() => null);
  if (!tab) return { holds: false, reason: REASON.tabClosed };
  const probe = await within(inPage(item.tabId, (key, x, y, left, top) => {
    const element = globalThis.__fawnReviewed?.get(key) ?? null;
    // Scrolling the page since the review is not a change: put the control back where it was reviewed.
    if (element?.isConnected) {
      const r = element.getBoundingClientRect();
      if (Math.floor(r.x) !== left || Math.floor(r.y) !== top) window.scrollBy({ left: Math.floor(r.x) - left, top: Math.floor(r.y) - top, behavior: 'instant' });
    }
    // What is under the point, followed into open shadow roots, must be the control or inside it, also
    // across shadow roots (up through each root's host).
    let hit = document.elementFromPoint(x, y);
    for (let inner; hit?.shadowRoot && (inner = hit.shadowRoot.elementFromPoint(x, y)) && inner !== hit;) hit = inner;
    let inside = false;
    for (let at = hit; at && element && !inside; at = at.parentNode ?? at.host) inside = at === element;
    return { bound: !!element, connected: !!element?.isConnected, sameUnderPoint: inside };
  }, [item.bound, item.point.x, item.point.y, (item.controlRect ?? item.rect).x, (item.controlRect ?? item.rect).y]).catch(() => null), PAGE_TIMEOUT_MS, SILENT);
  if (probe === SILENT) return { holds: false, reason: REASON.pageSilent };
  if (!probe || probe.documentId !== item.documentId || !probe.result.bound) return { holds: false, reason: REASON.documentReplaced };
  if (!probe.result.connected) return { holds: false, reason: REASON.controlGone };
  if (!probe.result.sameUnderPoint) return { holds: false, reason: REASON.covered };
  const image = await currentImage(item);
  if (image === null) return { holds: false, reason: REASON.noCapture };
  if (image !== item.image) return { holds: false, reason: REASON.pixelsChanged, image };
  let record;
  try { record = await viewRecord(item); } catch (error) { return { holds: false, reason: REASON.recordUnread(error.message) }; }
  if (record?.changes.length) return { holds: false, reason: REASON.stale(record.changes) };
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
  const shown = await within(inPage(tabId, () => true), PAGE_TIMEOUT_MS, null);
  if (!shown) throw new Error(REASON.pageSilent);
  const { documentId } = shown;
  // The person may already be looking at another tab, which leaves this one drawing nothing: capture it
  // in front, then put back the tab they were looking at.
  // `rect` is what is captured and compared: the control, or the area a person drew around it. `controlRect`
  // is where the control itself was, which is what a later check puts back in place.
  const rect = picked.area ?? picked.rect;
  const image = await capture(tabId, rect).catch(() => withVisibleTab(tabId, () => capture(tabId, rect)));
  const id = crypto.randomUUID();
  return write(id, () => ({ id, tabId, windowId: tab.windowId, documentId, url: picked.url, pageTitle: picked.pageTitle, rect, controlRect: picked.rect, point: picked.point,
    bound: picked.key, image, summary: picked.summary, state: STATE.PENDING, createdAt: now(), check: { at: now(), holds: true, reason: REASON.matches } }));
}

async function check(id) {
  const item = (await load())[id];
  if (!item || !isCheckable(item.state)) return item ?? null;
  return note(id, checked(await stillHolds(item)));
}

// A person approves what is pending, or approves again what the application's record showed was not applied.
// Both run the same checks. An approval again keeps the earlier results for the card.
async function approve(id) {
  const item = (await load())[id];
  const refused = item ? refusal(item.state, STATE.APPROVED) : REASON.notPending;
  if (refused) throw new Error(refused);
  const status = await stillHolds(item), check = checked(status);
  if (!status.holds) { await note(id, check); throw new Error(REASON.reviewAgain(status.reason)); }
  return move(id, before => ({ state: STATE.APPROVED, approvedAt: now(), check, outcome: null,
    earlier: [...(before.earlier ?? []), ...(before.outcome ? [before.outcome] : [])] }));
}

// The requests a click makes that could write: everything but GET, HEAD and OPTIONS, seen through the
// debugger's network events from the moment the click is armed. A request whose answer never reached the page,
// or whose answer was a server or gateway error, leaves the result unknown whatever the page then shows: the
// page's error is about the connection, not the application's decision.
function watchWrites(tabId) {
  const requests = new Map();
  const listener = (source, method, params) => {
    if (source.tabId !== tabId) return;
    if (method === 'Network.requestWillBeSent' && (requests.has(params.requestId) || !/^(GET|HEAD|OPTIONS)$/.test(params.request.method)))
      requests.set(params.requestId, { url: params.request.url.split('?')[0], method: requests.get(params.requestId)?.method ?? params.request.method, settled: false });
    const request = requests.get(params.requestId);
    if (!request) return;
    if (method === 'Network.responseReceived') Object.assign(request, { settled: true, status: params.response.status });
    if (method === 'Network.loadingFailed') Object.assign(request, { settled: true, failed: params.errorText || 'failed' });
  };
  chrome.debugger.onEvent?.addListener(listener);
  const all = () => [...requests.values()];
  return {
    ready: chrome.debugger.sendCommand(target(tabId), 'Network.enable', {}).catch(() => null),
    // Writes whose answer was lost, in words; empty when none was.
    lost: () => all().filter(r => r.failed || r.status >= 500).map(r => `${r.method} ${new URL(r.url).pathname}: ${r.failed ?? 'HTTP ' + r.status}`),
    settled: () => all().every(r => r.settled),
    stop: () => chrome.debugger.onEvent?.removeListener(listener),
  };
}

// Tabs where a finish is checking and clicking. Each frame has one fence, so one click at a time per tab:
// a second Finish there is refused before it spends its approval.
const clicking = new Set();
async function finish(id) {
  const tabId = (await load())[id]?.tabId;
  if (tabId != null && clicking.has(tabId)) throw new Error(REASON.tabBusy);
  if (tabId != null) clicking.add(tabId);
  try { return await clickAndSettle(id); }
  finally { if (tabId != null) clicking.delete(tabId); }
}
async function clickAndSettle(id) {
  // Spent from here on: whatever happens, this approval is not clicked again, and only the request that spent it clicks.
  const item = await move(id, { state: STATE.FINISHING, finishingBy: WORKER, finishingAt: now() });
  if (!item) throw new Error(REASON.notApproved);
  // If the item has gone by the time its result is known, the result is dropped: `move` does not bring it back.
  const settle = (state, text, detail = '', more = {}) => move(id, { ...more, state, outcome: { text, detail, at: now() } });
  const recipe = RECIPES[item.summary.recipe] ?? null;
  const noun = recipe?.noun ?? 'result';
  let writes = null;
  try {
    const tab = item.tabId == null ? null : await chrome.tabs.get(item.tabId).catch(() => null);
    if (!tab) return settle(STATE.BLOCKED, OUTCOME.blocked, REASON.tabClosed);
    {
      const status = await stillHolds(item);
      if (!status.holds) return settle(STATE.BLOCKED, OUTCOME.blocked, status.reason, { check: checked(status) });
      const before = recipe ? await within(resultState(item.tabId, recipe), PAGE_TIMEOUT_MS, null) : null;
      if (recipe && !before) return settle(STATE.BLOCKED, OUTCOME.blocked, REASON.pageSilent);
      writes = watchWrites(item.tabId);
      await within(writes.ready, PAGE_TIMEOUT_MS, null);
      // Arm the fence in every frame, click through the debugger, then read what the fence saw. A frame that
      // does not answer has no fence, so nothing is clicked.
      const armed = await within(everyFrame(item.tabId, key => { globalThis.__fawnFence = { element: globalThis.__fawnReviewed?.get(key) ?? null, clicked: false, blocked: false, down: false }; return true; }, [item.bound])
        .catch(() => null), PAGE_TIMEOUT_MS, null);
      if (!armed?.length || armed.some(frame => frame.result !== true)) return settle(STATE.BLOCKED, OUTCOME.blocked, REASON.pageSilent);
      const event = { x: item.point.x, y: item.point.y, button: 'left', clickCount: 1 };
      let seen = [], detached = null;
      // The debugger can let go of the tab during the click: the person cancels its bar, or opens DevTools.
      // Then neither the press nor the release is known to have gone out, or not.
      const onDetach = (source, reason) => { if (source.tabId === item.tabId) detached ??= reason || 'detached'; };
      chrome.debugger.onDetach?.addListener(onDetach);
      try {
        // The page may start navigating as the click lands, and then the browser can hold back its
        // acknowledgement of the event for a long time. The event has been sent either way. An error is not
        // swallowed as nothing: the event may have been dispatched before it.
        const send = type => within(chrome.debugger.sendCommand(target(item.tabId), 'Input.dispatchMouseEvent', { type, ...event })
          .catch(error => { detached ??= error.message; return null; }), 1500, null);
        await send('mousePressed');
        if (!detached) await send('mouseReleased');
      } finally {
        chrome.debugger.onDetach?.removeListener(onDetach);
        seen = await within(everyFrame(item.tabId, () => { const fence = globalThis.__fawnFence; globalThis.__fawnFence = null;
          return fence && { clicked: fence.clicked, blocked: fence.blocked, down: fence.down }; }).catch(() => []), 1000, []);
      }
      const clicked = seen.some(frame => frame.result?.clicked), blocked = seen.some(frame => frame.result?.blocked), down = seen.some(frame => frame.result?.down);
      if (detached && !clicked) return settle(STATE.UNKNOWN, OUTCOME.maybeSent(noun), REASON.debuggerDetached(detached));
      // A press that reached the control is input the page has had, whether or not a click followed: a control
      // can act on the press and take itself away. That is not "nothing was sent".
      if (down && !clicked) return settle(STATE.UNKNOWN, OUTCOME.maybeSent(noun), REASON.pressedNotClicked);
      // A page that navigated at once leaves no fence to read; the click was sent to the bound element's document.
      const navigated = seen.length === 0 || seen.every(frame => !frame.result);
      if (blocked || (!clicked && !navigated)) return settle(STATE.BLOCKED, OUTCOME.blocked, REASON.fenced);
      if (!recipe || !before) return settle(STATE.UNKNOWN, OUTCOME.notConfirmed(noun), REASON.noKnownSign);
      // Only what is new counts: a sign that was already there is this click's result only if the page
      // reloaded or more of them appeared. A refusal counts only once every write the click made has its
      // answer, and none of those answers was lost; it is seen twice in a row, so network events that
      // arrive just after the page shows its error are not missed.
      let refusalSeen = false;
      for (const deadline = Date.now() + RESULT_WINDOW_MS; ;) {
        const state = await resultSoon(item.tabId, recipe); // null while the page is navigating
        if (state) {
          const fresh = key => state[key] > 0 && (before[key] === 0 || state.document !== before.document || state[key] > before[key]);
          if (fresh('done')) return settle(STATE.CONFIRMED, recipe.doneLabel);
          const lost = writes.lost();
          if (lost.length) return settle(STATE.UNKNOWN, OUTCOME.notConfirmed(noun), REASON.answerLost(lost.join('; ')));
          if (fresh('refused') && writes.settled()) {
            if (refusalSeen) return settle(STATE.REFUSED, OUTCOME.refused, state.said);
            refusalSeen = true;
          } else refusalSeen = false;
        } else if (writes.lost().length) return settle(STATE.UNKNOWN, OUTCOME.notConfirmed(noun), REASON.answerLost(writes.lost().join('; ')));
        if (Date.now() >= deadline) return settle(STATE.UNKNOWN, OUTCOME.notConfirmed(noun), REASON.neitherSign);
        await new Promise(resolve => setTimeout(resolve, RESULT_POLL_MS));
      }
    }
  } catch (error) {
    return settle(STATE.UNKNOWN, OUTCOME.maybeSent(noun), REASON.lostTrack(error.message));
  } finally {
    writes?.stop();
  }
}

// Reconcile an item whose result is not known: read the application's own record of the target and say
// what it shows. It is given no way to click or write. The record shows it done: confirmed. It shows it not
// done, long enough after the click and the same on two reads: not applied, and a person may approve exactly
// this again if the approved terms still hold. Anything else: still unknown, with the reason.
async function reconcile(id) {
  const item = await move(id, { state: STATE.RECONCILING, reconcilingBy: WORKER });
  if (!item) throw new Error(REASON.notUnknown);
  const recipe = RECIPES[item.summary?.recipe];
  const words = { noun: recipe?.noun ?? 'result', doneLabel: recipe?.doneLabel ?? 'Confirmed' };
  let found;
  try {
    const firstReadAt = Date.now(), first = await viewRecord(item);
    if (!first) found = { unsupported: true };
    else {
      const reads = [first];
      if (!first.applied) { await new Promise(resolve => setTimeout(resolve, REREAD_MS)); reads.push(await viewRecord(item)); }
      const sent = Date.parse(item.finishingAt);
      found = { reads, sinceSentMs: Number.isFinite(sent) ? firstReadAt - sent : 0 };
    }
  } catch (error) { found = { failure: error.message }; }
  const answer = reconciled(found, words, now());
  return move(id, before => ({ ...answer, earlier: [...(before.earlier ?? []), ...(before.outcome ? [before.outcome] : [])] }));
}

const operations = {
  tabs: async () => (await chrome.tabs.query({})).filter(tab => /^https?:/.test(tab.url ?? ''))
    .map(tab => ({ id: tab.id, title: tab.title, url: tab.url, active: tab.active, windowId: tab.windowId })),
  items: async () => Object.values(await load()).sort((a, b) => a.createdAt.localeCompare(b.createdAt)),
  pick: message => whilePicking(message.tabId, () => pick(message.tabId)),
  check: message => check(message.id),
  approve: message => approve(message.id),
  finish: message => finish(message.id),
  reconcile: message => reconcile(message.id),
  remove: message => move(message.id, { state: REMOVED }),
};
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  const operation = operations[message?.inbox];
  if (!operation) return false;
  // Nothing runs before the items of an earlier worker or browser session are carried over: until then a tab id
  // may still name a tab from before a restart.
  recovered.then(() => operation(message)).then(value => respond({ ok: true, value }), error => respond({ ok: false, error: error.message }));
  return true;
});
// A tab that closes, or whose top document is replaced, takes its open items with it: nothing reviewed is
// left there to click, so they close as blocked and say why. An item that is being finished is left to its finish.
async function closeOpen(tabId, decide) {
  await recovered;
  for (const item of Object.values(await load())) if (item.tabId === tabId && decide(item)) await move(item.id, decide).catch(() => null);
}
chrome.tabs.onRemoved.addListener(tabId => closeOpen(tabId, item => whenTabCloses(item, now())));
// No tab event says "this is a new document", so on any change to a tab that has open items the tab is
// asked which document it shows. A tab that does not answer is left for the checks before a click.
chrome.tabs.onUpdated.addListener(async tabId => {
  await recovered;
  if (!Object.values(await load()).some(item => item.tabId === tabId && isOpen(item.state))) return;
  const shown = await within(chrome.scripting.executeScript({ target: { tabId, frameIds: [0] }, func: () => true, injectImmediately: true })
    .then(([frame]) => frame?.documentId ?? null, () => null), 1000, null);
  if (shown) await closeOpen(tabId, item => whenDocumentIs(item, shown, now()));
});
chrome.sidePanel?.setPanelBehavior?.({ openPanelOnActionClick: true }).catch(() => {});

// For the tests, which load this worker against a stand-in for `chrome`.
export { move };
