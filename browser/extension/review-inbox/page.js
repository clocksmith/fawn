// Runs in the extension's isolated world at document_start in every frame, so
// its capture listeners on window are registered before any page script's.
//
// It keeps three things for the inbox:
//   - the elements people picked for review, held by reference, so "the same
//     element" later means the same node and not something that looks like it.
//     The inbox deletes an entry when its item is closed or discarded. This
//     file knows the entries by key only; item states live in lifecycle.js;
//   - a fence: while one is armed, a trusted pointer event that does not land
//     on the bound element or something inside it (a label, an icon) is stopped
//     before the page or the element under it sees it. "Inside" is read from
//     the event's composed path, so a control in an open shadow root is found
//     even though the window sees only its host;
//   - a picker, started from the inbox, that lets a person choose the control.
globalThis.__fawnReviewed ??= new Map();
globalThis.__fawnFence = null;
for (const type of ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click', 'auxclick', 'dblclick', 'contextmenu']) {
  window.addEventListener(type, event => {
    const fence = globalThis.__fawnFence;
    if (!fence || !event.isTrusted) return;
    if (fence.element && event.composedPath().includes(fence.element)) {
      // A press that lands is input the control has had, even if no click follows.
      if (type === 'pointerdown' || type === 'mousedown') fence.down = true;
      if (type === 'click') fence.clicked = true;
      return;
    }
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
    // What the approval is for, in the application's own terms: this pull request at this head commit. The
    // inbox reads Forgejo's record of it (under the same root as the form) before the click and to reconcile.
    if (/^[0-9a-f]{40,64}$/.test(head)) summary.target = { base: action.origin + action.pathname.slice(0, merge.index), repo: merge[1], number: Number(merge[2]), head };
  } else {
    summary.title = `Click "${summary.control}"`;
    summary.lines = [`Button: ${summary.control}`, action ? `Submits to ${action.origin}${action.pathname}` : 'Not a form submission'];
  }
  return summary;
}

// One person picks one control. Hovering outlines it; a click chooses it and
// never reaches the page; Escape cancels. Shift and a click choose it and then
// let the person drag a box around it and what its meaning depends on (a total,
// a recipient): that whole area is what is compared, and the control is still
// what is clicked. Escape then keeps the control alone.
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
    // Whole pixels that enclose a rectangle.
    const whole = r => ({ x: Math.floor(r.x), y: Math.floor(r.y), width: Math.ceil(r.x + r.width) - Math.floor(r.x), height: Math.ceil(r.y + r.height) - Math.floor(r.y) });
    let chosen = null, dragFrom = null, drawn = null, ended = false;
    // A drag can end on another element or outside the window, and then the browser fires no click. The
    // release ends it. When the browser does make a click from that release, it dispatches it in the same task,
    // right after the pointerup: that one click is swallowed, so the page never sees it. The listener goes at
    // the next task, so a click the person makes afterwards is theirs and reaches the page.
    const swallowTrailingClick = () => {
      const once = event => { if (!event.isTrusted) return; swallow(event); window.removeEventListener('click', once, true); };
      window.addEventListener('click', once, true);
      setTimeout(() => window.removeEventListener('click', once, true), 0);
    };
    const done = element => {
      if (ended) return;
      ended = true;
      if (drawn) swallowTrailingClick();
      // Let the outline leave the screen before the browser captures the control.
      box.remove(); hint.remove();
      requestAnimationFrame(() => requestAnimationFrame(() => {
        const r = element.getBoundingClientRect(), rect = whole({ x: r.x, y: r.y, width: r.right - r.x, height: r.bottom - r.y });
        const summary = describeTarget(element);
        // The drawn box always covers the whole control, and stops at the edges of the window.
        let area = null;
        if (drawn) {
          const left = Math.max(0, Math.min(drawn.x, rect.x)), top = Math.max(0, Math.min(drawn.y, rect.y));
          const right = Math.min(window.innerWidth, Math.max(drawn.x + drawn.width, rect.x + rect.width)), bottom = Math.min(window.innerHeight, Math.max(drawn.y + drawn.height, rect.y + rect.height));
          area = whole({ x: left, y: top, width: right - left, height: bottom - top });
          summary.lines.push(`Also compared: the area you drew around the button, ${area.width} × ${area.height} px`);
        }
        const key = crypto.randomUUID();
        globalThis.__fawnReviewed.set(key, element);
        finish({ key, rect, ...(area ? { area } : {}), point: { x: rect.x + Math.floor(rect.width / 2), y: rect.y + Math.floor(rect.height / 2) },
          url: location.href, pageTitle: document.title, summary });
      }));
    };
    const choose = event => {
      swallow(event);
      if (chosen) { if (drawn) done(chosen); return; }
      const element = control(event.target);
      if (!element) { hint.textContent = 'That is not a button or link. Click the button you want approved. Esc cancels.'; return; }
      if (!event.shiftKey) { done(element); return; }
      chosen = element;
      hint.textContent = 'Now drag a box around the button and what it depends on, such as a total. Esc keeps just the button.';
    };
    // The drag is read from pointer events. Swallowing pointerdown cancels it for the page, and the browser
    // then sends no mousedown, mousemove or mouseup until the pointer is released, so those cannot be used.
    const press = event => {
      swallow(event);
      if (chosen && event.type === 'pointerdown') { dragFrom = { x: event.clientX, y: event.clientY }; drawn = null; }
      if (chosen && event.type === 'pointerup' && dragFrom) {
        const x = Math.min(dragFrom.x, event.clientX), y = Math.min(dragFrom.y, event.clientY);
        drawn = { x, y, width: Math.abs(event.clientX - dragFrom.x), height: Math.abs(event.clientY - dragFrom.y) };
        dragFrom = null;
        done(chosen);
      }
    };
    const move = event => {
      if (!chosen) { outline(event); return; }
      if (!dragFrom) return;
      const x = Math.min(dragFrom.x, event.clientX), y = Math.min(dragFrom.y, event.clientY);
      Object.assign(box.style, { display: 'block', left: x + 'px', top: y + 'px', width: Math.abs(event.clientX - dragFrom.x) + 'px', height: Math.abs(event.clientY - dragFrom.y) + 'px' });
    };
    const key = event => {
      if (event.key !== 'Escape') return;
      swallow(event);
      if (chosen) { drawn = null; done(chosen); } else finish({ cancelled: true });
    };
    const handlers = [['pointermove', move], ['click', choose], ['pointerdown', press], ['pointerup', press], ['mousedown', swallow], ['mouseup', swallow], ['keydown', key]];
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
