// Init-script observer: wraps WebGPU entry points without changing page code.
// Records adapter/device timing, every submit, every onSubmittedWorkDone
// resolution, whether a canvas texture was acquired before that completion,
// device errors and loss, and an independent rAF cadence.
(() => {
  if (typeof GPU === "undefined" || typeof GPUQueue === "undefined") return;
  const now = performance.now.bind(performance);
  let rngState = 0x13579bdf;
  Math.random = function deterministicRandom() {
    rngState ^= rngState << 13;
    rngState ^= rngState >>> 17;
    rngState ^= rngState << 5;
    return (rngState >>> 0) / 4294967296;
  };
  const B = {
    timeOrigin: performance.timeOrigin,
    adapterReqStart: null, adapterReqEnd: null, deviceEnd: null, adapterInfo: null,
    submits: 0, firstSubmitT: null, lastSubmitT: null,
    gctSincePrev: false, gctCount: 0, autoDonePending: false,
    completions: [], // [submitT, callT, resolveT, presented]
    firstPresentedDoneT: null,
    errors: [], devices: [], waiters: [], raf: [],
  };
  Date.now = function deterministicDateNow() {
    return 1700000000000 + B.raf.length * 16;
  };
  Object.defineProperty(globalThis, "__bench", { value: B, enumerable: false });

  const G = GPU.prototype;
  const reqAdapter = G.requestAdapter;
  G.requestAdapter = function (...args) {
    B.adapterReqStart ??= now();
    return reqAdapter.apply(this, args).then((adapter) => {
      B.adapterReqEnd ??= now();
      try {
        const i = adapter?.info;
        if (i && !B.adapterInfo) {
          B.adapterInfo = { vendor: i.vendor, architecture: i.architecture, device: i.device, description: i.description };
        }
      } catch {}
      return adapter;
    });
  };
  const reqDevice = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = function (...args) {
    return reqDevice.apply(this, args).then((device) => {
      B.deviceEnd ??= now();
      B.devices.push(device);
      device.addEventListener("uncapturederror", (e) =>
        B.errors.push({ kind: "uncapturederror", t: now(), message: String(e.error?.message ?? e.error) }));
      device.lost.then((info) => B.errors.push({ kind: "lost", t: now(), reason: info.reason, message: info.message }));
      return device;
    });
  };
  const Q = GPUQueue.prototype;
  const submit = Q.submit;
  Q.submit = function (...args) {
    const t = now();
    B.submits += 1;
    B.firstSubmitT ??= t;
    B.lastSubmitT = t;
    const r = submit.apply(this, args);
    if (B.gctSincePrev && !B.autoDonePending && !globalThis.__fawnManualOpTiming) {
      B.autoDonePending = true;
      const q = this;
      queueMicrotask(() => {
        B.autoDonePending = false;
        if (B.gctSincePrev) {
          Q.onSubmittedWorkDone.call(q);
        }
      });
    }
    return r;
  };
  const done = Q.onSubmittedWorkDone;
  Q.onSubmittedWorkDone = function (...args) {
    const call = now();
    const sub = B.lastSubmitT;
    const presented = B.gctSincePrev;
    B.gctSincePrev = false;
    const p = done.apply(this, args);
    p.then(() => {
      const r = now();
      if (B.completions.length < 60000) B.completions.push([sub, call, r, presented ? 1 : 0]);
      if (presented && B.firstPresentedDoneT === null) B.firstPresentedDoneT = r;
      const w = B.waiters;
      B.waiters = [];
      for (const f of w) f(r);
    }, (e) => B.errors.push({ kind: "onSubmittedWorkDone_rejected", t: now(), message: String(e) }));
    return p;
  };
  const gct = GPUCanvasContext.prototype.getCurrentTexture;
  GPUCanvasContext.prototype.getCurrentTexture = function (...args) {
    B.gctSincePrev = true;
    B.gctCount += 1;
    return gct.apply(this, args);
  };
  const tick = (t) => {
    if (B.raf.length < 60000) B.raf.push(t);
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
  addEventListener("error", (e) => B.errors.push({ kind: "pageerror", t: now(), message: String(e.message) }));
  addEventListener("unhandledrejection", (e) => B.errors.push({ kind: "unhandledrejection", t: now(), message: String(e.reason) }));
})();
