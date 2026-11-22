// Runs in the extension's isolated world at document_start in every frame, so
// its capture listeners on window are registered before any page script's.
// While a fence is armed, a trusted pointer event that does not land on the
// bound element is stopped before the page or the element under it sees it.
globalThis.__reviewed ??= new Map();
globalThis.__fence = null;
for (const type of ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click', 'auxclick', 'dblclick', 'contextmenu']) {
  window.addEventListener(type, event => {
    const fence = globalThis.__fence;
    if (!fence || !event.isTrusted) return;
    if (fence.element && event.target === fence.element) { if (type === 'click') fence.clicked = true; return; }
    fence.blocked = true;
    event.stopImmediatePropagation();
    event.preventDefault();
  }, true);
}
