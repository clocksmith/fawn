import * as ffi from "./bun-ffi.js";
import * as full from "./full.js";
import { createDoeNamespace } from '../doe-namespace.js';

const ffiDefaultPlatforms = new Set(["darwin", "linux"]);
const requestedBackend = process.env.DOE_BUN_WEBGPU_BACKEND ?? "";
// bun-ffi needs the Doe flat entry points (doeRequestAdapterFlat, ...),
// which only the Zig drop-in exports: webgpu.h passes its async callback
// structs by value, which Bun FFI cannot. Other providers (the C runtime) run
// through the N-API surface, which Bun loads.
const ffiLoaded = ffi.providerInfo().loaded && ffi.providerHasFlatEntryPoints();
// An explicit FFI request is honored or refused, never silently replaced.
if (requestedBackend === "ffi" && !ffiLoaded) {
  const info = ffi.providerInfo();
  throw new Error(
    `doe-gpu: DOE_BUN_WEBGPU_BACKEND=ffi needs a provider exporting the Doe flat entry points (the Zig drop-in); `
      + `${info.loaded ? `the loaded ${info.libraryFlavor} provider (${info.doeLibraryPath}) does not` : "no provider library loaded"}. `
      + "Unset it to use the N-API path, which runs every provider.",
  );
}
const runtime = requestedBackend === "full"
  ? full
  : (requestedBackend === "ffi" || ffiDefaultPlatforms.has(process.platform)) && ffiLoaded
    ? ffi
    : full;
const runtimeProvider = runtime === ffi ? "doe-ffi" : "doe-full";

export const doe = createDoeNamespace({
  requestDevice: runtime.requestDevice,
});

export const create = runtime.create;
export const createCanvasContext = runtime.createCanvasContext ?? full.createCanvasContext;
export const createInstance = runtime.createInstance;
export const globals = runtime.globals;
export const setupGlobals = runtime.setupGlobals;
export const requestAdapter = runtime.requestAdapter;
export const requestDevice = runtime.requestDevice;
export const providerInfo = () => ({
  ...runtime.providerInfo(),
  bunRuntimeProvider: runtimeProvider,
});
export const nativeFastPathInfo = runtime.nativeFastPathInfo ?? full.nativeFastPathInfo;
export const prewarmPreparedDispatches = runtime.prewarmPreparedDispatches ?? full.prewarmPreparedDispatches;
export const nativeQueueSyncInfo = runtime.nativeQueueSyncInfo ?? full.nativeQueueSyncInfo;
export const nativePipelineCacheInfo = runtime.nativePipelineCacheInfo ?? full.nativePipelineCacheInfo;
export const packagePipelineCacheFlush = runtime.packagePipelineCacheFlush ?? full.packagePipelineCacheFlush;
export const fastPathStats = runtime.fastPathStats;
export const preflightShaderSource = runtime.preflightShaderSource ?? full.preflightShaderSource;
export const setNativeTimeoutMs = runtime.setNativeTimeoutMs ?? full.setNativeTimeoutMs;
export const createDoeRuntime = runtime.createDoeRuntime;
export const runDawnVsDoeCompare = runtime.runDawnVsDoeCompare;
export const CANVAS_ALPHA_MODES = runtime.CANVAS_ALPHA_MODES ?? full.CANVAS_ALPHA_MODES;
export const CANVAS_TONE_MAPPING_MODES = runtime.CANVAS_TONE_MAPPING_MODES ?? full.CANVAS_TONE_MAPPING_MODES;
export const CANVAS_COLOR_SPACES = runtime.CANVAS_COLOR_SPACES ?? full.CANVAS_COLOR_SPACES;
export const normalizeOrigin2D = runtime.normalizeOrigin2D ?? full.normalizeOrigin2D;
export const normalizeCanvasConfiguration = runtime.normalizeCanvasConfiguration ?? full.normalizeCanvasConfiguration;
export const createBrowserSurfaceClasses = runtime.createBrowserSurfaceClasses ?? full.createBrowserSurfaceClasses;
export const createNativeBrowserCanvasBackend = runtime.createNativeBrowserCanvasBackend ?? full.createNativeBrowserCanvasBackend;

export default {
  ...runtime,
  CANVAS_ALPHA_MODES,
  CANVAS_TONE_MAPPING_MODES,
  CANVAS_COLOR_SPACES,
  normalizeOrigin2D,
  normalizeCanvasConfiguration,
  createCanvasContext,
  createBrowserSurfaceClasses,
  createNativeBrowserCanvasBackend,
  nativeFastPathInfo,
  prewarmPreparedDispatches,
  nativeQueueSyncInfo,
  nativePipelineCacheInfo,
  packagePipelineCacheFlush,
  fastPathStats,
  preflightShaderSource,
  setNativeTimeoutMs,
  doe,
};
