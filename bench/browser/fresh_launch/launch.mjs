#!/usr/bin/env node
// One fresh, headed browser launch measuring one page. Usage:
//   node launch.mjs --spec spec.json --out result.json
// The spec names the arm, browser executable, extra args, page, repo root and
// timing windows. Benchmark pages live under bench/browser/fixtures; cycle uses browser/chromium/resources
// and pinned static assets in ../assets.
import { PAGES } from "./pages.mjs";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import {
  createReadStream,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import http from "node:http";
import os from "node:os";
import { dirname, extname, join, sep } from "node:path";
import { inflateSync } from "node:zlib";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : undefined; };
const spec = JSON.parse(readFileSync(opt("--spec"), "utf8"));
const outPath = opt("--out");
const ROOT = spec.repoRoot;
const ASSETS = spec.assetsRoot ?? join(HERE, "..", "assets");
const require = createRequire(join(ROOT, "browser/chromium/package.json"));
const { chromium } = require("playwright-core");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const sha256 = (b) => createHash("sha256").update(b).digest("hex");



function serve() {
  const map = (p) => {
    if (p.startsWith("/fixtures/")) return join(ROOT, "bench/browser/fixtures", p.slice(10));
    if (p.startsWith("/browser/chromium/resources/")) return join(ROOT, p.slice(1));
    if (p.startsWith("/resources/")) return join(ROOT, "browser/chromium/resources", p.slice(11));
    if (p.startsWith("/harness/")) return join(HERE, p.slice(9));
    if (p.startsWith("/three/")) return join(ASSETS, "three", p.slice(7));
    if (p.startsWith("/babylon/")) return join(ASSETS, "babylon", p.slice(9));
    if (p.startsWith("/webgpu-samples/")) return join(ASSETS, "webgpu-samples", p.slice(16));
    if (p.startsWith("/doppler/")) return join(ASSETS, "doppler", p.slice(9));
    if (p.startsWith("/models/")) return join(ASSETS, "models", p.slice(8));
    if (p.startsWith("/assets/")) return join(ASSETS, p.slice(8));
    if (p === "/repo/bench/kernels/workgroup_atomic.wgsl") return join(ROOT, "bench/kernels/workgroup_atomic.wgsl");
    if (p === "/repo/examples/workgroup_atomic_commands.json") return join(ROOT, "examples/workgroup_atomic_commands.json");
    return null;
  };
  const types = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".svg": "image/svg+xml",
    ".wgsl": "text/plain; charset=utf-8",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".webp": "image/webp",
    ".wasm": "application/wasm",
    ".bin": "application/octet-stream",
    ".glb": "model/gltf-binary",
    ".webm": "video/webm",
    ".mp4": "video/mp4",
  };
  const served = {};
  const server = http.createServer((req, res) => {
    const p = decodeURIComponent(new URL(req.url, "http://x").pathname);
    const file = map(p);
    if (!file || file.includes(`..${sep}`) || !existsSync(file)) {
      res.statusCode = 404;
      res.end();
      return;
    }
    let st;
    try {
      st = statSync(file);
    } catch {
      res.statusCode = 404;
      res.end();
      return;
    }
    if (!st.isFile()) {
      res.statusCode = 404;
      res.end();
      return;
    }
    const ctype = types[extname(file).toLowerCase()] ?? "application/octet-stream";
    res.setHeader("content-type", ctype);
    res.setHeader("content-length", st.size);
    res.setHeader("cache-control", "no-store");
    if (st.size <= 2 * 1024 * 1024) {
      const body = readFileSync(file);
      served[p] = sha256(body);
      res.end(body);
    } else {
      served[p] = `size:${st.size}`;
      createReadStream(file).pipe(res);
    }
  });
  return new Promise((ok) => server.listen(0, "127.0.0.1", () => ok({ server, port: server.address().port, served })));
}

// Minimal PNG decoder (8-bit RGB/RGBA, non-interlaced), as Chromium screenshots are.
function decodePng(buf) {
  let off = 8, w = 0, h = 0, ct = 0; const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off); const type = buf.toString("ascii", off + 4, off + 8);
    const data = buf.subarray(off + 8, off + 8 + len);
    if (type === "IHDR") { w = data.readUInt32BE(0); h = data.readUInt32BE(4); ct = data[9]; if (data[8] !== 8 || data[12] !== 0) throw new Error("png format"); }
    else if (type === "IDAT") idat.push(data);
    off += 12 + len;
  }
  const bpp = ct === 6 ? 4 : ct === 2 ? 3 : (() => { throw new Error(`png color type ${ct}`); })();
  const raw = inflateSync(Buffer.concat(idat)); const stride = w * bpp;
  const out = Buffer.alloc(w * h * 4); const prev = Buffer.alloc(stride); const cur = Buffer.alloc(stride);
  for (let y = 0; y < h; y += 1) {
    const f = raw[y * (stride + 1)]; raw.copy(cur, 0, y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i += 1) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let v = cur[i];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x += 1) { for (let k = 0; k < 3; k += 1) out[(y * w + x) * 4 + k] = cur[x * bpp + k]; out[(y * w + x) * 4 + 3] = bpp === 4 ? cur[x * bpp + 3] : 255; }
    cur.copy(prev);
  }
  return { w, h, rgba: out };
}
function pixelStats(img) {
  const n = img.w * img.h; let sum = 0, sum2 = 0; const counts = new Map();
  for (let i = 0; i < n; i += 1) {
    const r = img.rgba[i * 4], g = img.rgba[i * 4 + 1], b = img.rgba[i * 4 + 2];
    const l = 0.2126 * r + 0.7152 * g + 0.0722 * b; sum += l; sum2 += l * l;
    const key = (r << 16) | (g << 8) | b; counts.set(key, (counts.get(key) ?? 0) + 1);
  }
  const modal = Math.max(...counts.values());
  const mean = sum / n;
  return { rgbaSha256: sha256(img.rgba), width: img.w, height: img.h, meanLuma: mean,
    stdLuma: Math.sqrt(Math.max(0, sum2 / n - mean * mean)), distinctColors: counts.size, nonModalFraction: 1 - modal / n };
}
function diffFraction(a, b) {
  let d = 0; const n = a.w * a.h;
  for (let i = 0; i < n; i += 1) {
    if (Math.abs(a.rgba[i * 4] - b.rgba[i * 4]) + Math.abs(a.rgba[i * 4 + 1] - b.rgba[i * 4 + 1]) + Math.abs(a.rgba[i * 4 + 2] - b.rgba[i * 4 + 2]) > 6) d += 1;
  }
  return d / n;
}

function psJson(script) {
  return JSON.parse(execFileSync("powershell", ["-NoProfile", "-NonInteractive", "-Command", script], { encoding: "utf8", timeout: 60000 }) || "null");
}

function parseKbValue(text, key) {
  const m = text.match(new RegExp(`^${key}:\\s+(\\d+)\\s+kB`, "m"));
  return m ? Number(m[1]) * 1024 : null;
}

function readLinuxDrmBytes(pid) {
  if (!pid) return null;
  const dir = `/proc/${pid}/fdinfo`;
  let maxTotal = 0;
  let found = false;
  try {
    for (const f of readdirSync(dir)) {
      try {
        const txt = readFileSync(join(dir, f), "utf8");
        if (!txt.includes("drm-driver:")) continue;
        found = true;
        let bytes = 0;
        for (const line of txt.split("\n")) {
          const m = line.match(/^drm-(?:total|resident)-(?:system|memory|gtt|vram):\s+(\d+)\s*(kB|KiB|MB|MiB|B)?/i);
          if (m) {
            const val = Number(m[1]);
            const unit = (m[2] || "B").toLowerCase();
            const mult = unit.startsWith("k") ? 1024 : unit.startsWith("m") ? 1024 * 1024 : 1;
            bytes += val * mult;
          }
        }
        if (bytes > maxTotal) maxTotal = bytes;
      } catch {}
    }
  } catch {}
  return found ? maxTotal : null;
}

function memorySnapshot(procs) {
  const pids = procs.map((p) => p.id);
  const gpu = procs.find((p) => p.type === "gpu");
  if (process.platform === "darwin") {
    const tmp = mkdtempSync(join(os.tmpdir(), "fp-"));
    const file = join(tmp, "fp.json");
    execFileSync("footprint", ["-j", file, ...pids.flatMap((p) => ["-p", String(p)])], { stdio: "ignore", timeout: 60000 });
    const fp = JSON.parse(readFileSync(file, "utf8")); if (process.env.KEEP_FP) writeFileSync(process.env.KEEP_FP, JSON.stringify(fp)); rmSync(tmp, { recursive: true, force: true });
    const unit = fp["bytes per unit"] ?? 1;
    const per = Object.fromEntries(fp.processes.map((p) => [p.pid, p.footprint * unit]));
    const gp = fp.processes.find((p) => p.pid === gpu?.id);
    // Graphics-tagged categories hold Metal allocations on unified memory.
    let gpuGraphics = null;
    if (gp?.categories) {
      gpuGraphics = 0;
      for (const [k, v] of Object.entries(gp.categories)) if (/\(graphics\)$|^IOSurface$|^IOAccelerator$/.test(k)) gpuGraphics += (v.dirty ?? 0) * unit;
    }
    return { source: "footprint", totalFootprintBytes: fp["total footprint"] * unit, perProcessFootprintBytes: per,
      gpuProcessFootprintBytes: per[gpu?.id] ?? null, gpuProcessGraphicsDirtyBytes: gpuGraphics,
      gpuProcessPeakFootprintBytes: (gp?.auxiliary?.phys_footprint_peak ?? null) };
  }
  if (process.platform === "linux") {
    const per = {};
    let totalPss = 0;
    let totalRss = 0;
    for (const pid of pids) {
      let pss = null;
      let rss = null;
      try {
        const smaps = readFileSync(`/proc/${pid}/smaps_rollup`, "utf8");
        pss = parseKbValue(smaps, "Pss");
        rss = parseKbValue(smaps, "Rss");
      } catch {}
      if (rss === null) {
        try {
          const st = readFileSync(`/proc/${pid}/status`, "utf8");
          rss = parseKbValue(st, "VmRSS");
        } catch {}
      }
      const pBytes = pss ?? rss ?? 0;
      const rBytes = rss ?? pss ?? 0;
      per[pid] = { workingSet: rBytes, private: pBytes };
      totalPss += pBytes;
      totalRss += rBytes;
    }
    const drmBytes = readLinuxDrmBytes(gpu?.id);
    return {
      source: "proc_smaps_rollup+drm_fdinfo",
      totalPrivateBytes: totalPss,
      totalWorkingSetBytes: totalRss,
      perProcess: per,
      gpuProcessPrivateBytes: per[gpu?.id]?.private ?? null,
      gpuProcessWorkingSetBytes: per[gpu?.id]?.workingSet ?? null,
      gpuProcessDedicatedGpuBytes: drmBytes,
      gpuProcessSharedGpuBytes: 0,
    };
  }
  if (process.platform === "win32") {
    const list = pids.join(",");
    const rows = psJson(`Get-Process -Id ${list} -ErrorAction SilentlyContinue | Select-Object Id,WorkingSet64,PrivateMemorySize64 | ConvertTo-Json -Compress`);
    const arr = Array.isArray(rows) ? rows : rows ? [rows] : [];
    let gpuMem = null;
    if (gpu) {
      gpuMem = psJson(`$c = Get-Counter -Counter '\\GPU Process Memory(pid_${gpu.id}_*)\\Dedicated Usage','\\GPU Process Memory(pid_${gpu.id}_*)\\Shared Usage' -ErrorAction SilentlyContinue; $d=0;$s=0; foreach($x in $c.CounterSamples){ if($x.Path -like '*dedicated usage'){ $d+=$x.CookedValue } else { $s+=$x.CookedValue } }; @{dedicated=$d;shared=$s} | ConvertTo-Json -Compress`);
    }
    const per = Object.fromEntries(arr.map((r) => [r.Id, { workingSet: r.WorkingSet64, private: r.PrivateMemorySize64 }]));
    return { source: "Get-Process+GPU Process Memory counters",
      totalPrivateBytes: arr.reduce((s, r) => s + r.PrivateMemorySize64, 0),
      totalWorkingSetBytes: arr.reduce((s, r) => s + r.WorkingSet64, 0),
      perProcess: per, gpuProcessPrivateBytes: per[gpu?.id]?.private ?? null,
      gpuProcessWorkingSetBytes: per[gpu?.id]?.workingSet ?? null,
      gpuProcessDedicatedGpuBytes: gpuMem?.dedicated ?? null, gpuProcessSharedGpuBytes: gpuMem?.shared ?? null };
  }
  return null;
}

function providerModules(gpuPid) {
  if (!gpuPid) return { error: "no gpu process" };
  try {
    if (process.platform === "darwin") {
      const out = execFileSync("lsof", ["-p", String(gpuPid), "-Fn"], { encoding: "utf8", timeout: 30000 });
      const names = out.split("\n").filter((l) => l.startsWith("n/")).map((l) => l.slice(1));
      return { doeRuntimeMapped: names.some((n) => /libwebgpu_doe_c\.dylib$/.test(n)),
        doeCompilerMapped: names.some((n) => /libdoe_compiler\.dylib$/.test(n)),
        matches: [...new Set(names.filter((n) => /doe|webgpu/i.test(n)).map((n) => n.split("/").pop()))],
        loadedArtifacts: [...new Set(names.filter((n) => /doe|webgpu/i.test(n)))].sort() };
    }
    if (process.platform === "linux") {
      const maps = readFileSync(`/proc/${gpuPid}/maps`, "utf8");
      const names = [...new Set(maps.split("\n").map((l) => l.trim().split(/\s+/)[5]).filter((p) => p && p.startsWith("/")))];
      return {
        doeRuntimeMapped: names.some((n) => /libwebgpu_doe_c\.so/.test(n)),
        doeCompilerMapped: names.some((n) => /libdoe_compiler\.so/.test(n)),
        matches: [...new Set(names.filter((n) => /doe|webgpu|vulkan|mesa/i.test(n)).map((n) => n.split("/").pop()))],
        loadedArtifacts: names.filter((n) => /doe|webgpu|vulkan|mesa/i.test(n)).sort(),
      };
    }
    if (process.platform === "win32") {
      const mods = psJson(`(Get-Process -Id ${gpuPid}).Modules | Select-Object -ExpandProperty ModuleName | ConvertTo-Json -Compress`) ?? [];
      const list = Array.isArray(mods) ? mods : [mods];
      return { doeRuntimeMapped: list.some((n) => /webgpu_doe_c\.dll$/i.test(n)),
        doeCompilerMapped: list.some((n) => /doe_compiler\.dll$/i.test(n)),
        matches: list.filter((n) => /doe|webgpu|d3d12|dxil|dxcompiler/i.test(n)) };
    }
  } catch (error) { return { error: String(error) }; }
  return {};
}

function pct(values, q) {
  if (!values || !values.length) return null;
  const s = [...values].sort((a, b) => a - b);
  return s[Math.max(0, Math.ceil(s.length * q) - 1)];
}

const result = { spec, startedAt: new Date().toISOString(), host: { platform: process.platform, arch: process.arch,
  cpus: os.cpus()[0]?.model, node: process.version }, failures: [] };
const fail = (stage, e) => result.failures.push({ stage, error: String(e?.stack ?? e) });
let browser = null; let srv = null; let beforeBrowserStat = null;
const page_ = PAGES[spec.page];
try {
  if (!page_) throw new Error(`unknown page: ${spec.page}`);
  const headless = spec.headless ?? false;
  if (typeof headless !== 'boolean') throw new Error('headless must be a boolean');
  if (spec.autoCompletion !== undefined && typeof spec.autoCompletion !== 'boolean') throw new Error('autoCompletion must be a boolean');
  if (spec.particleOracle !== undefined && typeof spec.particleOracle !== 'boolean') throw new Error('particleOracle must be a boolean');
  if (spec.trace !== undefined && typeof spec.trace !== 'boolean') throw new Error('trace must be a boolean');
  if (spec.particleOracle && spec.page !== 'heavy_particles') throw new Error('particleOracle requires heavy_particles');
  srv = await serve();
  const artifactStat = statSync(spec.browserPath, { bigint: true });
  beforeBrowserStat = Object.fromEntries(["dev", "ino", "size", "mtimeNs", "ctimeNs"].map(key => [key, String(artifactStat[key])]));
  const launchStartEpoch = Date.now();
  browser = await chromium.launch({ executablePath: spec.browserPath, headless, chromiumSandbox: true,
    ignoreDefaultArgs: ["--enable-unsafe-swiftshader"], args: [...spec.args, "--window-size=1280,800", "--window-position=40,40"], timeout: 120000 });
  result.launchMs = Date.now() - launchStartEpoch;
  result.browserVersion = browser.version();
  const cdp = await browser.newBrowserCDPSession();
  const procInfo = async () => (await cdp.send("SystemInfo.getProcessInfo")).processInfo.map((p) => ({ ...p, type: String(p.type).toLowerCase() }));
  const context = await browser.newContext({ viewport: null });
  if (spec.autoCompletion === false) await context.addInitScript(() => { globalThis.__benchAutoCompletion = false; });
  if (spec.particleOracle) {
    const oraclePath = join(ROOT, 'bench/oracles/particle_step.js');
    result.particleOracleSourceSha256 = sha256(readFileSync(oraclePath));
    await context.addInitScript({ path: oraclePath });
  }
  await context.addInitScript({ path: join(HERE, "observer.js") });
  const page = await context.newPage();
  const consoleErrors = [];
  page.on("console", (m) => { if (m.type() === "error") consoleErrors.push(m.text().slice(0, 400)); });
  page.on("pageerror", (e) => consoleErrors.push(`pageerror: ${String(e).slice(0, 400)}`));
  page.on("crash", () => fail("page", "page crashed"));
  if (!headless) await page.bringToFront();
  const url = `http://127.0.0.1:${srv.port}${page_.url}`;
  const gotoStart = Date.now();
  await page.goto(url, { waitUntil: "load", timeout: 90000 });
  // Cold first job.
  const firstJob = await page.waitForFunction((kind) => {
    const B = globalThis.__bench;
    const st = document.querySelector("#status")?.textContent ?? "";
    if (/^error/i.test(st)) return { error: st };
    if (kind === "compute") {
      const S = globalThis.__computeBench;
      if (S?.state === "error") return { error: S.error };
      return S?.state === "ready" ? { t: S.firstJobDoneT } : false;
    }
    if (kind === "doppler") {
      const D = globalThis.__dopplerBench;
      if (D?.state === "error") return { error: D.error };
      return D?.state === "ready" ? { t: D.firstJobDoneT } : false;
    }
    return B?.firstPresentedDoneT != null ? { t: B.firstPresentedDoneT } : false;
  }, page_.kind, { timeout: 90000, polling: 10 }).then((h) => h.jsonValue());
  if (firstJob.error) throw new Error(`first job: ${firstJob.error}`);
  const early = await page.evaluate(() => ({ timeOrigin: performance.timeOrigin, B: { adapterReqStart: __bench.adapterReqStart,
    adapterReqEnd: __bench.adapterReqEnd, deviceEnd: __bench.deviceEnd, firstSubmitT: __bench.firstSubmitT,
    adapterInfo: __bench.adapterInfo }, dpr: devicePixelRatio, inner: [innerWidth, innerHeight],
    canvases: [...document.querySelectorAll("canvas")].map((c) => [c.id, c.width, c.height]) }));
  result.cold = { firstJobMs: firstJob.t, adapterRequestStartMs: early.B.adapterReqStart, adapterMs: early.B.adapterReqEnd,
    deviceMs: early.B.deviceEnd, firstSubmitMs: early.B.firstSubmitT,
    launchToFirstJobMs: (early.timeOrigin - launchStartEpoch) + firstJob.t, gotoIssuedToNavStartMs: early.timeOrigin - gotoStart };
  result.adapterInfo = early.B.adapterInfo; result.dpr = early.dpr; result.viewport = early.inner; result.canvases = early.canvases;

  await sleep(spec.warmupMs);
  let procs0 = await procInfo();
  let traceComplete;
  if (spec.trace) {
    traceComplete = new Promise(resolve => cdp.once('Tracing.tracingComplete', resolve));
    await cdp.send('Tracing.start', { categories: 'gpu,cc,viz,benchmark,disabled-by-default-gpu.service', transferMode: 'ReturnAsStream' });
  }
  const c0 = await page.evaluate(() => ({ n: __bench.completions.length, r: __bench.raf.length, pageCalls: __bench.pageCompletionCalls, injectedCalls: __bench.injectedCompletionCalls, t: performance.now() }));
  if (spec.particleOracle) await page.evaluate(() => globalThis.__fawnParticleOracleArm());
  let window_ = {};

  if (spec.soakSeconds && spec.soakSeconds > 0) {
    // Long soak mode: sample slices every soakIntervalSeconds (default 30s) across soakSeconds.
    const totalSec = Number(spec.soakSeconds);
    const stepSec = Number(spec.soakIntervalSeconds ?? 30);
    const slices = [];
    let prevProcs = procs0;
    let prevMark = c0;
    let elapsed = 0;
    while (elapsed < totalSec) {
      const waitSec = Math.min(stepSec, totalSec - elapsed);
      await sleep(waitSec * 1000);
      elapsed += waitSec;
      const curProcs = await procInfo();
      const curMark = await page.evaluate((m) => {
        const B = __bench;
        const tNow = performance.now();
        return {
          n: B.completions.length,
          r: B.raf.length,
          t: tNow,
          dtMs: tNow - m.t,
          completions: B.completions.slice(m.n),
          raf: B.raf.slice(m.r),
        };
      }, prevMark);
      const byPidPrev = Object.fromEntries(prevProcs.map((p) => [p.id, p]));
      const sliceCpu = {};
      for (const p of curProcs) {
        sliceCpu[p.type] = (sliceCpu[p.type] ?? 0) + (p.cpuTime - (byPidPrev[p.id]?.cpuTime ?? 0));
      }
      const pres = curMark.completions.filter((c) => c[3] === 1);
      const lats = pres.map((c) => c[2] - c[0]);
      const ivs = [];
      for (let i = 1; i < pres.length; i += 1) ivs.push(pres[i][2] - pres[i - 1][2]);
      const mem = memorySnapshot(curProcs);
      const fCount = pres.length;
      slices.push({
        elapsedSeconds: elapsed,
        durationMs: curMark.dtMs,
        frames: fCount,
        completionP50Ms: pct(lats, 0.5),
        completionP95Ms: pct(lats, 0.95),
        completionP99Ms: pct(lats, 0.99),
        intervalP50Ms: pct(ivs, 0.5),
        intervalP95Ms: pct(ivs, 0.95),
        intervalP99Ms: pct(ivs, 0.99),
        gpuCpuMsPerFrame: fCount ? (sliceCpu.gpu ?? 0) * 1000 / fCount : null,
        totalCpuMsPerFrame: fCount ? Object.values(sliceCpu).reduce((a, b) => a + b, 0) * 1000 / fCount : null,
        memory: mem,
      });
      prevProcs = curProcs;
      prevMark = { n: curMark.n, r: curMark.r, t: curMark.t };
    }
    result.soakSlices = slices;
  } else if (page_.kind === "animated") {
    await sleep(spec.windowMs);
  } else if (page_.kind === "imagelab") {
    window_ = await page.evaluate(async (n) => {
      const s = document.getElementById("strength"); const B = __bench; const lat = [], gpu = [];
      for (let i = 0; i < n; i += 1) {
        const v = i === n - 1 ? 70 : (i % 2 ? 40 : 90);
        const t = performance.now();
        const wait = new Promise((ok, ko) => { const to = setTimeout(() => ko(new Error("update timeout")), 5000); B.waiters.push((r) => { clearTimeout(to); ok(r); }); });
        s.value = String(v); s.dispatchEvent(new Event("input", { bubbles: true }));
        const r = await wait; lat.push(r - t);
        const c = B.completions[B.completions.length - 1]; gpu.push(c[2] - c[0]);
        await new Promise((ok) => setTimeout(ok, 0));
      }
      return { opLatencyMs: lat, opSubmitToDoneMs: gpu };
    }, spec.imageLabOps);
  } else if (page_.kind === "compute") {
    const fin = await page.evaluate(async (n) => globalThis.__computeBench.runOps(n), spec.computeOps);
    window_ = await page.evaluate(() => ({ ops: __computeBench.ops, finalOracle: __computeBench.finalOracle, oracle: __computeBench.oracle }));
    if (!fin?.matched) fail("oracle", `final oracle mismatch ${JSON.stringify(fin)}`);
    if (!window_.oracle?.matched) fail("oracle", `first oracle mismatch ${JSON.stringify(window_.oracle)}`);
  } else if (page_.kind === "doppler") {
    const fin = await page.evaluate(async (n) => globalThis.__dopplerBench.runOps(n), spec.dopplerOps ?? 12);
    window_ = await page.evaluate(() => ({ ops: __dopplerBench.ops, finalOracle: __dopplerBench.finalOracle, oracle: __dopplerBench.oracle }));
    if (!fin?.matched) fail("oracle", `final doppler oracle mismatch ${JSON.stringify(fin)}`);
    if (!window_.oracle?.matched) fail("oracle", `first doppler oracle mismatch ${JSON.stringify(window_.oracle)}`);
  }

  const procs1 = await procInfo();
  const w = await page.evaluate((c0) => {
    const B = __bench; const t1 = performance.now();
    return { dtMs: t1 - c0.t, completions: B.completions.slice(c0.n), raf: B.raf.slice(c0.r), errors: B.errors,
      pageCompletionCalls: B.pageCompletionCalls - c0.pageCalls, injectedCompletionCalls: B.injectedCompletionCalls - c0.injectedCalls,
      submits: B.submits, gctCount: B.gctCount, status: document.querySelector("#status")?.textContent ?? "",
      failureVisible: !!document.querySelector("#failure") && !document.querySelector("#failure").hidden,
      frameText: document.querySelector("#frame")?.textContent ?? "",
      benchResult: globalThis.__fawnBenchResult ?? null };
  }, c0);
  if (spec.trace) {
    await cdp.send('Tracing.end');
    const completed = await traceComplete;
    if (!completed.stream || completed.dataLossOccurred) throw new Error('trace stream missing or trace data lost');
    const chunks = [];
    for (;;) {
      const part = await cdp.send('IO.read', { handle: completed.stream });
      chunks.push(part.base64Encoded ? Buffer.from(part.data, 'base64') : Buffer.from(part.data));
      if (part.eof) break;
    }
    await cdp.send('IO.close', { handle: completed.stream });
    const bytes = Buffer.concat(chunks);
    const trace = JSON.parse(bytes.toString('utf8'));
    const path = outPath.replace(/\.json$/, '.trace');
    if (path === outPath) throw new Error('trace requires a .json receipt path');
    writeFileSync(path, bytes);
    const counts = {};
    const flushCounts = {};
    const flushSites = new Set(['CommandBuffer::Flush', 'CommandBufferStub::OnAsyncFlush',
      'GpuChannelMessageFilter::FlushDeferredRequests', 'CommandBufferHelper::Flush',
      'CommandBufferProxyImpl::Flush', 'GpuChannel::Flush', 'SkiaOutputSurfaceImpl::FlushGpuTasks']);
    for (const event of trace.traceEvents ?? []) {
      if (event.ph !== 'B' && event.ph !== 'X') continue;
      if (/WaitForCommandsToBeScheduled/.test(event.name ?? '')) counts[event.name] = (counts[event.name] ?? 0) + 1;
      if (flushSites.has(event.name)) flushCounts[event.name] = (flushCounts[event.name] ?? 0) + 1;
    }
    result.trace = { path, sha256: sha256(bytes), dataLossOccurred: false, iosurfaceSchedulingEvents: counts, flushEvents: flushCounts,
      scope: 'trace spans the measurement window; counts cover named instrumented sites only, may overlap, and are not totals for all waits or flushes' };
  }
  if (spec.particleOracle) {
    result.particleSampleOracle = await page.evaluate(() => globalThis.__fawnParticleOracleRead());
    if (!result.particleSampleOracle?.matched) fail('particle sample oracle', JSON.stringify(result.particleSampleOracle));
  }
  const byPid0 = Object.fromEntries(procs0.map((p) => [p.id, p]));
  const cpu = {};
  for (const p of procs1) {
    const d = p.cpuTime - (byPid0[p.id]?.cpuTime ?? 0);
    cpu[p.type] = (cpu[p.type] ?? 0) + d;
  }
  const comps = w.completions;
  // Texture acquisition is a proxy for submitted canvas work, not visible presentation.
  const presented = comps.filter((c) => c[3] === 1);
  const frames = page_.kind === "imagelab"
    ? window_.opLatencyMs.length
    : (page_.kind === "compute" || page_.kind === "doppler")
      ? window_.ops.length
      : presented.length;
  result.window = { durationMs: w.dtMs, frames, pageCompletionCalls: w.pageCompletionCalls, injectedCompletionCalls: w.injectedCompletionCalls, completions: comps.length, presentedCompletions: presented.length,
    cpuSecondsByType: cpu, gpuCpuMsPerFrame: frames ? (cpu.gpu ?? 0) * 1000 / frames : null,
    rendererCpuMsPerFrame: frames ? (cpu.renderer ?? 0) * 1000 / frames : null,
    browserCpuMsPerFrame: frames ? (cpu.browser ?? 0) * 1000 / frames : null,
    totalCpuMsPerFrame: frames ? Object.values(cpu).reduce((a, b) => a + b, 0) * 1000 / frames : null };
  // Per-frame / per-operation samples.
  const lat = (page_.kind === "animated")
    ? presented.map((c) => c[2] - c[0])
    : page_.kind === "imagelab"
      ? window_.opLatencyMs
      : window_.ops.map((o) => o.completeMs);
  const intervals = []; for (let i = 1; i < presented.length; i += 1) intervals.push(presented[i][2] - presented[i - 1][2]);
  const rafInt = []; for (let i = 1; i < w.raf.length; i += 1) rafInt.push(w.raf[i] - w.raf[i - 1]);
  result.samples = { completionLatencyMs: lat, completedFrameIntervalMs: intervals, rafIntervalMs: rafInt,
    imageLabSubmitToDoneMs: window_.opSubmitToDoneMs ?? null, computeEncodeSubmitMs: window_.ops?.map((o) => o.encodeSubmitMs) ?? null };
  if (page_.kind === "compute" || page_.kind === "doppler") {
    result.oracle = { first: window_.oracle, final: window_.finalOracle };
  } else if (page_.expectBenchOracle) {
    const matched = Boolean(w.benchResult?.exactOracleMatched);
    result.oracle = {
      first: { matched, detail: w.benchResult },
      final: { matched, detail: w.benchResult },
    };
    if (!matched) fail("oracle", `benchResult oracle failed: ${JSON.stringify(w.benchResult)}`);
  } else {
    result.oracle = null;
  }
  result.timingSemantics = { completion: "host submit to queue callback delivery", frameInterval: "callback delivery interval", raf: "requestAnimationFrame cadence", presentation: "not measured", gpuExecution: "not measured", canvasMarker: "texture acquired, not visible presentation" };
  result.pageState = { status: w.status, frameText: w.frameText, failureVisible: w.failureVisible, submits: w.submits, gctCount: w.gctCount };
  result.memory = memorySnapshot(procs1);
  result.processes = procs1.map((p) => ({ type: p.type, id: p.id }));
  const gpuPid = procs1.find((p) => p.type === "gpu")?.id;
  result.provider = providerModules(gpuPid);
  // Visual checksum: centre clip, two captures 400 ms apart.
  const vp = await page.evaluate(() => [innerWidth, innerHeight]);
  const clip = { x: Math.floor(vp[0] * 0.3), y: Math.floor(vp[1] * 0.3), width: Math.floor(vp[0] * 0.4), height: Math.floor(vp[1] * 0.3) };
  const shotA = await page.screenshot({ clip, type: "png" }); await sleep(400);
  const shotB = await page.screenshot({ clip, type: "png" });
  const ia = decodePng(shotA), ib = decodePng(shotB);
  result.visual = { clip, a: pixelStats(ia), b: pixelStats(ib), changedFraction: diffFraction(ia, ib) };
  if (spec.keepPng) { writeFileSync(outPath.replace(/\.json$/, ".png"), shotB); }
  // Drain: page-owned hook when present, otherwise the observed devices' queues.
  result.drain = await page.evaluate(async () => {
    try {
      if (typeof globalThis.__fawnDiagnosticStopAndDrain === "function") {
        const d = await globalThis.__fawnDiagnosticStopAndDrain(); return { via: "page", gpuCompletion: d?.gpuCompletion ?? "unknown" };
      }
      for (const d of __bench.devices) await d.queue.onSubmittedWorkDone();
      return { via: "observer", gpuCompletion: "completed", devices: __bench.devices.length };
    } catch (e) { return { via: "error", gpuCompletion: "unknown", error: String(e) }; }
  });
  if (result.drain.gpuCompletion !== "completed") fail("drain", JSON.stringify(result.drain));
  const errs = w.errors.filter((e) => !(e.kind === "lost" && e.reason === "destroyed"));
  result.pageErrors = errs; result.consoleErrors = consoleErrors;
  if (errs.length) fail("page", JSON.stringify(errs).slice(0, 800));
  if (/^error/i.test(w.status) || w.failureVisible) fail("page", `status=${w.status}`);
  if (frames < 1) fail("window", "no completed frames/operations");
  if (spec.expectDoe !== undefined && result.provider.doeRuntimeMapped !== spec.expectDoe) fail("provider", `doeRuntimeMapped=${result.provider.doeRuntimeMapped} expected ${spec.expectDoe}`);
  result.served = srv.served;
} catch (error) {
  fail("run", error);
  try {
    if (browser && !result.provider) {
      const cdpErr = await browser.newBrowserCDPSession();
      const procsErr = (await cdpErr.send("SystemInfo.getProcessInfo")).processInfo.map((p) => ({ ...p, type: String(p.type).toLowerCase() }));
      if (!result.memory) result.memory = memorySnapshot(procsErr);
      result.processes = procsErr.map((p) => ({ type: p.type, id: p.id }));
      const gpuPidErr = procsErr.find((p) => p.type === "gpu")?.id;
      result.provider = providerModules(gpuPidErr);
    }
  } catch {}
} finally {
  try { if (browser) await browser.close(); } catch (e) { fail("close", e); }
  if (srv) srv.server.close();
  result.finishedAt = new Date().toISOString();
  // The shared provenance implementation runs after timed work and browser close.
  // Requested library paths are not evidence that those bytes were loaded.
  try {
    result.provenance = JSON.parse(execFileSync(process.env.DOE_BENCH_PYTHON ?? "python3",
      ["-m", "bench.lib.receipt_provenance"], { cwd: ROOT, encoding: "utf8", timeout: 60000,
        input: JSON.stringify({ executedAt: result.finishedAt,
          implementation: { name: spec.arm, binaryPath: spec.browserPath, expectedStat: beforeBrowserStat,
            componentPaths: Object.fromEntries((result.provider?.loadedArtifacts ?? []).map((path, i) => [`loaded-${i}`, path])) },
          machine: { gpu: result.adapterInfo?.description || result.adapterInfo?.device || null } }) }));
  } catch (error) { fail("provenance", error); }
  result.ok = result.failures.length === 0;
  mkdirSync(dirname(outPath), { recursive: true });
  writeFileSync(outPath, JSON.stringify(result));
  console.log(`${spec.arm} ${spec.page} rep=${spec.rep} ok=${result.ok} firstJob=${result.cold?.firstJobMs?.toFixed?.(1)} frames=${result.window?.frames} gpuCpuMs/frame=${result.window?.gpuCpuMsPerFrame?.toFixed?.(3)} doe=${result.provider?.doeRuntimeMapped}${result.ok ? "" : " FAIL " + result.failures.map((f) => f.stage + ":" + f.error.slice(0, 200)).join(" | ")}`);
  process.exit(result.ok ? 0 : 1);
}
