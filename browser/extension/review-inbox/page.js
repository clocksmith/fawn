// Runs in the extension's isolated world at document_start in every frame, so
// its capture listeners on window are registered before any page script's.
//
// It keeps three things for the inbox:
//   - the elements people picked for review, held by reference, so "the same
//     element" later means the same node and not something that looks like it.
//     The inbox deletes an entry when its item is closed or discarded. This
//     file knows the entries by key only; item states live in lifecycle.js;
//   - a fence: while one is armed, a trusted pointer event that does not land
//     on the bound element is stopped before the page or the element under it
//     sees it;
//   - a picker, started from the inbox, that lets a person choose the control.
globalThis.__fawnReviewed ??= new Map();
globalThis.__fawnFence = null;
for (const type of ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click', 'auxclick', 'dblclick', 'contextmenu']) {
  window.addEventListener(type, event => {
    const fence = globalThis.__fawnFence;
    if (!fence || !event.isTrusted) return;
    if (fence.element && event.target === fence.element) { if (type === 'click') fence.clicked = true; return; }
    fence.blocked = true;
    event.stopImmediatePropagation();
    event.preventDefault();
  }, true);
}

// What the picked control will do, read from the page around it. A recipe names
// the application's own signs of the result; without one the inbox can still
// bind and deliver the click but cannot say what the application did.
function describeTarget(element) {
  const label = (element.innerText || element.value || element.getAttribute('aria-label') || element.title || '').trim().replace(/\s+/g, ' ');
  const form = element.form ?? element.closest?.('form') ?? null;
  // A long label is cut at a word and marked as cut; the picture of the button shows all of it.
  const short = label.length > 80 ? label.slice(0, 80).replace(/\s+\S*$/, '') + '…' : label;
  const summary = { control: short || element.tagName.toLowerCase(), lines: [], recipe: null };
  let action = null;
  // The attribute, not the property: a form control named "action" shadows `form.action`.
  const declared = form?.getAttribute('action');
  try { action = form ? new URL(declared || location.href, location.href) : null; } catch { action = null; }
  const merge = action && /\/([^/]+\/[^/]+)\/pulls\/(\d+)\/merge$/.exec(action.pathname);
  if (merge && element.type === 'submit') {
    // Forgejo and Gitea: the merge form carries the head commit it was opened for.
    const head = form.querySelector('input[name=head_commit_id]')?.value ?? '';
    // The heading also holds the number and an Edit button; the card wants the title alone.
    const title = (document.querySelector('#issue-title-display, .issue-title')?.innerText || document.title).trim().replace(/\s+/g, ' ')
      .replace(/\s*#\d+(\s+Edit)?\s*$/, '').slice(0, 140);
    summary.recipe = 'forgejo-merge';
    summary.title = `Merge pull request #${merge[2]} in ${merge[1]}`;
    summary.lines = [title, head ? `At commit ${head.slice(0, 10)}` : 'Head commit not shown', `Button: ${summary.control}`];
  } else {
    summary.title = `Click "${summary.control}"`;
    summary.lines = [`Button: ${summary.control}`, action ? `Submits to ${action.origin}${action.pathname}` : 'Not a form submission'];
  }
  return summary;
}

// One person picks one control. Hovering outlines it; a click chooses it and
// never reaches the page; Escape cancels.
function pick() {
  return new Promise(resolve => {
    const box = document.createElement('div');
    box.style.cssText = 'position:fixed;z-index:2147483647;pointer-events:none;border:2px solid #1a7f5a;background:#1a7f5a22;border-radius:3px;display:none';
    const hint = document.createElement('div');
    hint.textContent = 'Click the button you want approved. Esc cancels.';
    hint.style.cssText = 'position:fixed;z-index:2147483647;pointer-events:none;left:12px;bottom:12px;max-width:calc(100vw - 24px);box-sizing:border-box;padding:8px 12px;border-radius:6px;background:#12291c;color:#fff;font:13px system-ui,sans-serif';
    document.documentElement.append(box, hint);
    // Only something a person can press can be approved; the rest of the page is not a target.
    const control = node => node?.closest?.('button, input[type=submit], input[type=button], input[type=image], a[href], summary, [role=button]') ?? null;
    const outline = event => {
      const over = control(event.target);
      if (!over) { box.style.display = 'none'; return; }
      const r = over.getBoundingClientRect();
      Object.assign(box.style, { display: 'block', left: r.left - 2 + 'px', top: r.top - 2 + 'px', width: r.width + 'px', height: r.height + 'px' });
    };
    const swallow = event => { event.stopImmediatePropagation(); event.preventDefault(); };
    const finish = result => {
      for (const [type, handler] of handlers) window.removeEventListener(type, handler, true);
      box.remove(); hint.remove();
      resolve(result);
    };
    const choose = event => {
      swallow(event);
      const element = control(event.target);
      if (!element) { hint.textContent = 'That is not a button or link. Click the button you want approved. Esc cancels.'; return; }
      // Let the outline leave the screen before the browser captures the control.
      box.remove(); hint.remove();
      requestAnimationFrame(() => requestAnimationFrame(() => {
        const r = element.getBoundingClientRect();
        // Whole pixels that enclose the control.
        const rect = { x: Math.floor(r.x), y: Math.floor(r.y), width: Math.ceil(r.right) - Math.floor(r.x), height: Math.ceil(r.bottom) - Math.floor(r.y) };
        const key = crypto.randomUUID();
        globalThis.__fawnReviewed.set(key, element);
        finish({ key, rect, point: { x: rect.x + Math.floor(rect.width / 2), y: rect.y + Math.floor(rect.height / 2) },
          url: location.href, pageTitle: document.title, summary: describeTarget(element) });
      }));
    };
    const key = event => { if (event.key === 'Escape') { swallow(event); finish({ cancelled: true }); } };
    const handlers = [['mousemove', outline], ['click', choose], ['mousedown', swallow], ['mouseup', swallow], ['pointerdown', swallow], ['pointerup', swallow], ['keydown', key]];
    for (const [type, handler] of handlers) window.addEventListener(type, handler, true);
  });
}

if (window === window.top) {
  chrome.runtime.onMessage.addListener((message, sender, respond) => {
    if (message?.fawn !== 'pick') return false;
    pick().then(respond);
    return true;
  });
}
