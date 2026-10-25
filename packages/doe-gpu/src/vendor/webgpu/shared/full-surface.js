import { NATIVE_ERROR_FILTERS as ERROR_FILTER_MAP } from './provider-error-scopes.js';
import {
  UINT32_MAX,
  failValidation,
  failUnsupported,
  describeResourceLabel,
  initResource,
  markInvalid,
  isInvalidResource,
  assertObject,
  assertArray,
  assertBoolean,
  assertNonEmptyString,
  assertIntegerInRange,
  assertOptionalIntegerInRange,
  assertLiveResource,
  destroyResource,
  releaseWhenCollected,
  normalizeOrigin3D,
  normalizeExtent3D,
} from './resource-lifecycle.js';
import {
  assertBufferDescriptor,
  assertTextureSize,
  assertBindGroupResource,
  normalizeRequestAdapterOptions,
  normalizeRequestDeviceDescriptor,
  normalizeSamplerDescriptor,
  normalizeTextureViewDescriptor,
  normalizeTextureFormat,
  normalizeTextureDescriptor,
  normalizeTextureDimension,
  normalizeQuerySetDescriptor,
  normalizePrimitiveState,
  normalizeDepthStencilState,
  normalizeVertexBufferLayouts,
  normalizeBindGroupLayoutEntry,
} from './validation.js';
import {
  shaderCheckFailure,
} from './compiler-errors.js';
import {
  WEBGPU_DEFAULT_LIMITS,
} from './capabilities.js';

function validateWriteBufferInput(data, dataOffset, size, path) {
  const isSharedArrayBuffer = typeof SharedArrayBuffer !== 'undefined' && data instanceof SharedArrayBuffer;
  assertIntegerInRange(dataOffset, path, 'dataOffset', { min: 0 });
  if (
    !ArrayBuffer.isView(data)
    && !(data instanceof ArrayBuffer)
    && !isSharedArrayBuffer
    && !Buffer.isBuffer(data)
  ) {
    failValidation(path, 'data must be a TypedArray, DataView, ArrayBuffer, SharedArrayBuffer, or Buffer');
  }
  if (size !== undefined) {
    assertIntegerInRange(size, path, 'size', { min: 0 });
  }
  const byteSource = ArrayBuffer.isView(data)
    ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
    : new Uint8Array(data);
  if (dataOffset === 0 && size === undefined) {
    return byteSource;
  }
  const elementSize = ArrayBuffer.isView(data) && typeof data.BYTES_PER_ELEMENT === 'number'
    ? data.BYTES_PER_ELEMENT
    : 1;
  const start = dataOffset * elementSize;
  const end = size !== undefined
    ? start + size * elementSize
    : byteSource.byteLength;
  if (end > byteSource.byteLength) {
    failValidation(path, `data range ${dataOffset}+${size ?? ((byteSource.byteLength - start) / elementSize)} exceeds source byteLength ${byteSource.byteLength}`);
  }
  return byteSource.subarray(start, end);
}

function fastDefaultWriteBufferView(data, dataOffset, size) {
  if (dataOffset !== 0 || size !== undefined || !ArrayBuffer.isView(data)) {
    return null;
  }
  if (typeof data.BYTES_PER_ELEMENT !== 'number') {
    return null;
  }
  return data;
}

function normalizeWriteBufferBatchEntry(entry, index) {
  const path = `GPUQueue.__doeWriteBufferBatch.entries[${index}]`;
  const object = assertObject(entry, path, 'entry');
  const bufferOffset = object.offset ?? object.bufferOffset ?? 0;
  assertIntegerInRange(bufferOffset, path, 'offset', { min: 0 });
  const dataOffset = object.dataOffset ?? 0;
  const view = validateWriteBufferInput(object.data, dataOffset, object.size, path);
  return {
    buffer: object.buffer,
    bufferOffset,
    view,
  };
}

function escapeRegexLiteral(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// https://gpuweb.github.io/gpuweb/#abstract-opdef-resolving-gputextureaspect:
// the format a single-aspect view of a depth/stencil format has, or null when
// the format lacks that aspect.
const DEPTH_ASPECT_FORMATS = Object.freeze({
  depth16unorm: 'depth16unorm',
  depth24plus: 'depth24plus',
  depth32float: 'depth32float',
  'depth24plus-stencil8': 'depth24plus',
  'depth32float-stencil8': 'depth32float',
});
const STENCIL_FORMATS = new Set(['stencil8', 'depth24plus-stencil8', 'depth32float-stencil8']);

function textureAspectFormat(format, aspect) {
  if (aspect === 'depth-only') return DEPTH_ASPECT_FORMATS[format] ?? null;
  if (aspect === 'stencil-only') return STENCIL_FORMATS.has(format) ? 'stencil8' : null;
  return format;
}

function normalizeOrigin2DForExternalCopy(origin, path) {
  if (origin === undefined || origin === null) {
    return { x: 0, y: 0 };
  }
  if (Array.isArray(origin)) {
    return { x: origin[0] ?? 0, y: origin[1] ?? 0 };
  }
  const object = assertObject(origin, path, 'origin');
  return {
    x: object.x ?? 0,
    y: object.y ?? 0,
  };
}

function normalizeOrigin3DForExternalCopy(origin, path) {
  if (origin === undefined || origin === null) {
    return { x: 0, y: 0, z: 0 };
  }
  if (Array.isArray(origin)) {
    return { x: origin[0] ?? 0, y: origin[1] ?? 0, z: origin[2] ?? 0 };
  }
  const object = assertObject(origin, path, 'origin');
  return {
    x: object.x ?? 0,
    y: object.y ?? 0,
    z: object.z ?? 0,
  };
}

function assertShaderEntryPoint(shader, entryPoint, path) {
  const code = shader?._code;
  if (typeof code !== 'string' || code.length === 0) {
    return;
  }
  const pattern = new RegExp(`\\bfn\\s+${escapeRegexLiteral(entryPoint)}\\s*\\(`);
  if (!pattern.test(code)) {
    failValidation(path, `entry point "${entryPoint}" not found in shader module`);
  }
}

function inferShaderEntryPoint(shader, explicitEntryPoint, stage, path, label) {
  if (explicitEntryPoint !== undefined && explicitEntryPoint !== null) {
    return assertNonEmptyString(explicitEntryPoint, path, label);
  }
  const code = shader?._code;
  if (typeof code === 'string' && code.length > 0) {
    const names = [];
    const pattern = new RegExp(
      `@${escapeRegexLiteral(stage)}\\b(?:\\s+@\\w+(?:\\([^)]*\\))?)*\\s+fn\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(`,
      'g',
    );
    let match;
    while ((match = pattern.exec(code)) !== null) {
      names.push(match[1]);
    }
    if (names.length === 1) {
      return names[0];
    }
  }
  return 'main';
}

const FAST_PATH_NOT_APPLIED = Symbol('fastPathNotApplied');

function operationError(message) {
  if (typeof DOMException === 'function') {
    return new DOMException(message, 'OperationError');
  }
  const error = new Error(message);
  error.name = 'OperationError';
  return error;
}

function createDeviceLostState(device) {
  if (device._lost instanceof Promise) {
    return;
  }
  device._lostInfo = null;
  device._resolveLost = null;
  device._lost = new Promise((resolve) => {
    device._resolveLost = resolve;
  });
}

function resolveDeviceLost(device, reason = 'unknown', message = '') {
  createDeviceLostState(device);
  if (device._lostInfo !== null) {
    return device._lostInfo;
  }
  const info = new GPUDeviceLostInfo(reason, message);
  device._lostInfo = info;
  device._resolveLost?.(info);
  return info;
}

// A destroyed or lost device: WebGPU makes its operations inert. Creation
// returns invalid objects, queue operations do nothing, nothing throws, and no
// error is generated.
function isDeviceLost(device) {
  return device != null && (device._destroyed === true || (device._lostInfo ?? null) !== null);
}

function abortError(message) {
  if (typeof DOMException === 'function') {
    return new DOMException(message, 'AbortError');
  }
  const error = new Error(message);
  error.name = 'AbortError';
  return error;
}

function shouldRouteDeviceError(device) {
  if (Array.isArray(device._errorScopes) && device._errorScopes.length > 0) {
    return true;
  }
  if (typeof device._onuncapturederror === 'function') {
    return true;
  }
  const listeners = device._eventListeners;
  const uncapturedListeners = listeners instanceof Map ? listeners.get('uncapturederror') : null;
  return uncapturedListeners instanceof Map && uncapturedListeners.size > 0;
}

// Reports an error the package detected. Provider errors reported before it
// are delivered first, so every scope keeps the first error in call order.
function captureDeviceError(device, filter, error) {
  device._drainProviderErrors?.();
  return deliverDeviceError(device, filter, error);
}

// Delivers one error to the innermost scope with its filter, else as an
// uncapturederror event. A lost device discards errors (WebGPU), and while
// finish() collects its errors they are held for it instead.
function deliverDeviceError(device, filter, error) {
  if (isDeviceLost(device)) {
    return false;
  }
  if (Array.isArray(device._errorCollection)) {
    device._errorCollection.push({ filter, error });
    return true;
  }
  const scopes = Array.isArray(device._errorScopes) ? device._errorScopes : [];
  for (let index = scopes.length - 1; index >= 0; index -= 1) {
    const scope = scopes[index];
    if (scope.filter !== filter) {
      continue;
    }
    if (scope.error === null) {
      scope.error = error;
    }
    return true;
  }

  queueUncapturedError(device, error);
  return false;
}

// uncapturederror fires from a queued task (WebGPU), so no GPU call runs a
// listener: a listener that throws cannot interrupt the call that generated
// the error, nor drop errors still being delivered.
function queueUncapturedError(device, error) {
  const fire = () => {
    const event = new GPUUncapturedErrorEvent('uncapturederror', { error });
    if (typeof device._onuncapturederror === 'function') {
      invokeListener(device, device._onuncapturederror, event);
    }
    dispatchDeviceEvent(device, 'uncapturederror', event);
  };
  if (typeof setImmediate === 'function') {
    setImmediate(fire);
  } else {
    setTimeout(fire, 0);
  }
}

// A listener's exception is reported as uncaught, as EventTarget does, and
// the remaining listeners still run.
function invokeListener(device, listener, event) {
  try {
    if (typeof listener === 'function') {
      listener.call(device, event);
    } else if (typeof listener?.handleEvent === 'function') {
      listener.handleEvent(event);
    }
  } catch (error) {
    queueMicrotask(() => {
      throw error;
    });
  }
}

// A validation error outside creation: reported where an error scope or
// uncapturederror listener observes it, thrown otherwise.
function reportValidationError(device, message) {
  if (!shouldRouteDeviceError(device)) {
    throw new Error(message);
  }
  captureDeviceError(device, 'validation', new GPUValidationError(message));
}

// Runs run() (an encoder or bundle finish) collecting the errors it reports,
// package and provider alike, in order: finish() reports at most one.
function collectDeviceErrors(device, run) {
  if (typeof device?._collectErrors !== 'function') {
    return { result: run(), errors: [] };
  }
  return device._collectErrors(run);
}

function gpuErrorFromCreationFailure(filter, error) {
  const message = error?.message ?? String(error);
  if (filter === 'out-of-memory') {
    return new GPUOutOfMemoryError(message);
  }
  if (filter === 'internal') {
    return new GPUInternalError(message);
  }
  return new GPUValidationError(message);
}

// TypeError and RangeError are content-timeline exceptions in WebGPU, and an
// unsupported Doe capability is not a validation failure: those always throw.
function isReportableCreationError(error) {
  return !(error instanceof TypeError)
    && !(error instanceof RangeError)
    && error?.code !== 'DOE_UNSUPPORTED';
}

// The kind of destroyed resource a bind group entry names, or null: a view
// made after its texture's destroy(), a destroyed GPUTexture, or a destroyed
// GPUBuffer given directly or as a GPUBufferBinding.
function destroyedBindingKind(resource) {
  if (resource?._textureDestroyed === true
    || (resource?._resourceLabel === 'GPUTexture' && resource._destroyed && !resource._invalid)) return 'texture';
  const buffer = resource?._resourceLabel === 'GPUBuffer' ? resource : resource?.buffer;
  if (buffer?._resourceLabel === 'GPUBuffer' && buffer._destroyed && !buffer._invalid) return 'buffer';
  return null;
}

function labelOf(descriptor) {
  return typeof descriptor?.label === 'string' ? descriptor.label : '';
}

function passThrough(error) {
  return error;
}

// Attributes an invalid texture still reports, taken from its descriptor.
function invalidTextureMeta(descriptor) {
  const size = descriptor?.size;
  const extent = Array.isArray(size)
    ? { width: size[0], height: size[1], depthOrArrayLayers: size[2] }
    : (size ?? {});
  const count = (value, fallback) => (Number.isInteger(value) && value >= 0 ? value : fallback);
  return {
    width: count(extent.width, 1),
    height: count(extent.height, 1),
    depthOrArrayLayers: count(extent.depthOrArrayLayers, 1),
    mipLevelCount: count(descriptor?.mipLevelCount, 1),
    sampleCount: count(descriptor?.sampleCount, 1),
    dimension: typeof descriptor?.dimension === 'string' ? descriptor.dimension : '2d',
    format: typeof descriptor?.format === 'string' ? descriptor.format : 'rgba8unorm',
    usage: count(descriptor?.usage, 0),
    viewFormats: Array.isArray(descriptor?.viewFormats) ? descriptor.viewFormats : [],
  };
}

function isObjectRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function resolveFastBufferBindingResource(resource, path, index) {
  if (!isObjectRecord(resource)) {
    return null;
  }
  if ('buffer' in resource) {
    const buffer = assertLiveResource(resource.buffer, path, 'GPUBuffer');
    const offset = assertOptionalIntegerInRange(
      resource.offset ?? 0,
      path,
      `descriptor.entries[${index}].resource.offset`,
      { min: 0 },
    );
    if (resource.size !== undefined) {
      assertIntegerInRange(resource.size, path, `descriptor.entries[${index}].resource.size`, { min: 1 });
      return null;
    }
    return { buffer, offset: offset ?? 0 };
  }
  if ('_native' in resource) {
    const label = describeResourceLabel(resource);
    // Only a GPUBuffer is a buffer binding; a GPUTexture binds its default view.
    if (label !== 'GPUBuffer') {
      return null;
    }
    return {
      buffer: assertLiveResource(resource, path, 'GPUBuffer'),
      offset: 0,
    };
  }
  return null;
}

function tryCreateBufferBindGroupFast4(backend, device, layoutNative, bindGroupDescriptor) {
  if (typeof backend.deviceCreateBufferBindGroupFlat4 !== 'function') {
    return FAST_PATH_NOT_APPLIED;
  }
  if (bindGroupDescriptor.label !== undefined && bindGroupDescriptor.label !== '') {
    return FAST_PATH_NOT_APPLIED;
  }
  const path = 'GPUDevice.createBindGroup';
  const entries = bindGroupDescriptor.entries ?? [];
  if (!Array.isArray(entries) || entries.length > 4) {
    return FAST_PATH_NOT_APPLIED;
  }
  let b0 = 0;
  let b1 = 0;
  let b2 = 0;
  let b3 = 0;
  let buffer0 = null;
  let buffer1 = null;
  let buffer2 = null;
  let buffer3 = null;
  let offset0 = 0;
  let offset1 = 0;
  let offset2 = 0;
  let offset3 = 0;
  for (let index = 0; index < entries.length; index += 1) {
    const entry = entries[index];
    if (!isObjectRecord(entry)) {
      return FAST_PATH_NOT_APPLIED;
    }
    const resource = resolveFastBufferBindingResource(entry.resource, path, index);
    if (resource === null) {
      return FAST_PATH_NOT_APPLIED;
    }
    const binding = assertIntegerInRange(entry.binding, path, `descriptor.entries[${index}].binding`, { min: 0, max: UINT32_MAX });
    if (index === 0) {
      b0 = binding;
      buffer0 = resource.buffer;
      offset0 = resource.offset;
    } else if (index === 1) {
      b1 = binding;
      buffer1 = resource.buffer;
      offset1 = resource.offset;
    } else if (index === 2) {
      b2 = binding;
      buffer2 = resource.buffer;
      offset2 = resource.offset;
    } else {
      b3 = binding;
      buffer3 = resource.buffer;
      offset3 = resource.offset;
    }
  }
  const native = backend.deviceCreateBufferBindGroupFlat4(
    device,
    layoutNative,
    entries.length,
    b0,
    buffer0,
    offset0,
    b1,
    buffer1,
    offset1,
    b2,
    buffer2,
    offset2,
    b3,
    buffer3,
    offset3,
    bindGroupDescriptor.label || undefined,
  );
  return native === undefined ? FAST_PATH_NOT_APPLIED : native;
}

class GPUError extends Error {
  constructor(message) { super(message); this.name = 'GPUError'; }
}
class GPUValidationError extends GPUError {
  constructor(message) { super(message); this.name = 'GPUValidationError'; }
}
class GPUOutOfMemoryError extends GPUError {
  constructor(message) { super(message ?? ''); this.name = 'GPUOutOfMemoryError'; }
}
class GPUInternalError extends GPUError {
  constructor(message) { super(message ?? ''); this.name = 'GPUInternalError'; }
}
class GPUPipelineError extends DOMException {
  constructor(message, options) {
    super(message, 'GPUPipelineError');
    this.reason = options?.reason ?? 'internal';
  }
}

function wrapPipelineError(error) {
  if (error instanceof TypeError || error instanceof RangeError) {
    return error;
  }
  if (error instanceof Error) {
    const message = error.message ?? '';
    const reason = error.reason
      ?? (error.name === 'GPUValidationError'
        || message.startsWith('GPUDevice.createComputePipeline')
        || message.startsWith('GPUDevice.createComputePipelineAsync')
        || message.startsWith('GPUDevice.createRenderPipeline')
        || message.startsWith('GPUDevice.createRenderPipelineAsync')
        || message.startsWith('createComputePipeline requires')
        || message.startsWith('createRenderPipeline requires')
        || message.startsWith('createComputePipeline:')
        || message.startsWith('createRenderPipeline:')
        ? 'validation'
        : 'internal');
    return new GPUPipelineError(message, { reason });
  }
  return error;
}
class GPUDeviceLostInfo {
  constructor(reason, message) {
    this.reason = reason ?? 'unknown';
    this.message = message ?? '';
  }
}
class GPUUncapturedErrorEvent extends Event {
  constructor(type, init) {
    super(type);
    this.error = init?.error ?? null;
  }
}

function ensureDeviceEventListeners(device) {
  if (!(device._eventListeners instanceof Map)) {
    device._eventListeners = new Map();
  }
  return device._eventListeners;
}

// EventTarget subset: a listener is a function or an object with handleEvent;
// the once option removes it before its first call; duplicates are ignored.
function addDeviceEventListener(device, type, listener, options) {
  if (listener === null || (typeof listener !== 'function' && typeof listener !== 'object')) {
    return;
  }
  const listeners = ensureDeviceEventListeners(device);
  const typeListeners = listeners.get(type) ?? new Map();
  if (!typeListeners.has(listener)) {
    typeListeners.set(listener, { once: typeof options === 'object' && options !== null && Boolean(options.once) });
  }
  listeners.set(type, typeListeners);
}

function removeDeviceEventListener(device, type, listener) {
  const listeners = device._eventListeners;
  if (!(listeners instanceof Map)) {
    return;
  }
  const typeListeners = listeners.get(type);
  if (!(typeListeners instanceof Map)) {
    return;
  }
  typeListeners.delete(listener);
  if (typeListeners.size === 0) {
    listeners.delete(type);
  }
}

function dispatchDeviceEvent(device, type, event) {
  const listeners = device._eventListeners;
  if (!(listeners instanceof Map)) {
    return;
  }
  const typeListeners = listeners.get(type);
  if (!(typeListeners instanceof Map)) {
    return;
  }
  for (const [listener, entry] of [...typeListeners]) {
    if (entry.once) {
      removeDeviceEventListener(device, type, listener);
    }
    invokeListener(device, listener, event);
  }
}

const EMPTY_ADAPTER_INFO = Object.freeze({
  vendor: '',
  architecture: '',
  device: '',
  description: '',
  subgroupMinSize: 0,
  subgroupMaxSize: 0,
});

const CORE_FEATURES_AND_LIMITS = 'core-features-and-limits';
const ALIGNMENT_LIMIT_NAMES = Object.freeze(new Set([
  'minUniformBufferOffsetAlignment',
  'minStorageBufferOffsetAlignment',
]));
const MAX_ALIGNMENT_LIMIT = 2 ** 31;
const MAX_QUERY_SET_COUNT = 4096;

function isPowerOfTwo(value) {
  return Number.isInteger(value) && value > 0 && (value & (value - 1)) === 0;
}

function createFullSurfaceClasses({
  globals,
  backend,
  encoderClasses,
}) {
  let classes = null;
  function normalizeErrorFilter(filter, path) {
    const encoded = ERROR_FILTER_MAP[filter];
    if (encoded === undefined) {
      failValidation(path, `invalid filter "${filter}"; must be "validation", "out-of-memory", or "internal"`);
    }
    return encoded;
  }

  // === WebGPU error semantics for synchronous creation ===
  // With an error scope or an uncapturederror listener, a creation that fails
  // validation reports exactly one error and returns an invalid object; with
  // neither, it throws as it always has. `create` returns the new object, or
  // null when the provider rejected the descriptor.
  //
  // One ordered owner: every provider error reaches the package's log through
  // the native uncaptured-error callback (no native error scopes are pushed),
  // and the package delivers provider and package errors to its own scopes in
  // the order they happened. The log's sequence is read without a native call,
  // so a successful creation costs two reads. Errors the provider reported
  // during the call make the result invalid and are delivered as they are; a
  // rejection without a report gets one package error.
  function createReportingErrors(device, path, create, makeInvalid, toThrown = passThrough) {
    if (isDeviceLost(device)) {
      return makeInvalid();
    }
    const logged = device?._providerErrorLog === true;
    if (logged) {
      device._drainProviderErrors();
    }
    const mark = logged ? backend.providerErrorMark() : null;
    if (!shouldRouteDeviceError(device)) {
      let created;
      try {
        created = create();
      } catch (error) {
        if (logged) device._takeProviderErrorsSince(mark);
        throw toThrown(error);
      }
      const reported = logged ? device._takeProviderErrorsSince(mark) : [];
      if (created !== null) {
        if (reported.length > 0) markInvalid(created, created.label);
        return created;
      }
      throw toThrown(reported.length > 0 ? new Error(`${path}: ${reported[0].message}`) : providerRejection(path));
    }
    let created;
    try {
      created = create();
    } catch (error) {
      if (!isReportableCreationError(error)) {
        throw toThrown(error);
      }
      captureDeviceError(device, 'validation', gpuErrorFromCreationFailure('validation', error));
      return makeInvalid();
    }
    const reported = logged ? device._takeProviderErrorsSince(mark) : [];
    for (const error of reported) {
      deliverDeviceError(device, error.type, error);
    }
    if (created !== null) {
      if (reported.length > 0) markInvalid(created, created.label);
      return created;
    }
    // Without a provider log (Bun) nothing shows whether the provider
    // reported, and its native scopes are not read back, so the package
    // reports the rejection itself.
    if (reported.length === 0) {
      deliverDeviceError(device, 'validation', new GPUValidationError(`${path}: the provider rejected the descriptor without reporting an error`));
    }
    return makeInvalid();
  }

  function providerRejection(path) {
    return new Error(`${path}: the provider rejected the descriptor`);
  }

  // Creates and throws the provider's error instead of reporting it: where
  // WebGPU throws or rejects (async pipeline creation, and synchronous
  // pipeline creation that no error scope or listener observes).
  function createWithProviderMessage(device, path, create) {
    if (device?._providerErrorLog !== true) {
      const value = create();
      if (value !== null) return value;
      throw providerRejection(path);
    }
    device._drainProviderErrors();
    const mark = backend.providerErrorMark();
    let value;
    try {
      value = create();
    } catch (error) {
      device._takeProviderErrorsSince(mark);
      throw error;
    }
    const reported = device._takeProviderErrorsSince(mark);
    if (reported.length > 0) {
      throw new Error(`${path}: ${reported[0].message}`);
    }
    if (value === null) {
      throw providerRejection(path);
    }
    return value;
  }

  function reportInvalidUse(device, path, resource) {
    reportValidationError(
      device,
      `${path}: ${describeResourceLabel(resource)} is invalid because its creation failed validation`,
    );
  }

  function resolveDeviceFeatures(adapter, descriptor) {
    const requiredFeatures = descriptor?.requiredFeatures ?? [];
    const adapterFeatures = adapter.features;
    for (const feature of requiredFeatures) {
      if (!adapterFeatures.has(feature)) {
        throw new TypeError(`GPUAdapter.requestDevice: required feature "${feature}" is not supported by this adapter`);
      }
    }
    if (requiredFeatures.length > 0) {
      return Object.freeze(new Set(requiredFeatures));
    }
    if (adapterFeatures.has(CORE_FEATURES_AND_LIMITS)) {
      return Object.freeze(new Set([CORE_FEATURES_AND_LIMITS]));
    }
    return Object.freeze(new Set());
  }

  const STAGE_STORAGE_LIMITS = [
    ['maxStorageBuffersPerShaderStage', ['maxStorageBuffersInVertexStage', 'maxStorageBuffersInFragmentStage']],
    ['maxStorageTexturesPerShaderStage', ['maxStorageTexturesInVertexStage', 'maxStorageTexturesInFragmentStage']],
  ];

  function validateRequiredLimitValue(name, value, adapterLimits) {
    if (!(name in WEBGPU_DEFAULT_LIMITS)) {
      throw operationError(`GPUAdapter.requestDevice: requiredLimits.${name} is not a supported limit`);
    }
    if (!Number.isFinite(value) || value < 0 || value > Number.MAX_SAFE_INTEGER) {
      throw new TypeError(`GPUAdapter.requestDevice: requiredLimits.${name} must be a non-negative safe integer`);
    }
    if (!Number.isInteger(value)) {
      throw operationError(`GPUAdapter.requestDevice: requiredLimits.${name} must be an integer`);
    }
    const adapterValue = adapterLimits[name] ?? WEBGPU_DEFAULT_LIMITS[name];
    if (ALIGNMENT_LIMIT_NAMES.has(name)) {
      if (value > MAX_ALIGNMENT_LIMIT || !isPowerOfTwo(value) || value < adapterValue) {
        throw operationError(`GPUAdapter.requestDevice: requiredLimits.${name} is not supported by this adapter`);
      }
      return;
    }
    if (value > adapterValue) {
      throw operationError(`GPUAdapter.requestDevice: requiredLimits.${name} is not supported by this adapter`);
    }
  }

  function resolveDeviceLimits(adapter, descriptor) {
    const limits = { ...WEBGPU_DEFAULT_LIMITS };
    const requiredLimits = descriptor?.requiredLimits;
    if (requiredLimits === undefined) {
      return Object.freeze(limits);
    }
    const adapterLimits = adapter.limits;
    for (const [name, value] of Object.entries(requiredLimits)) {
      if (value === undefined) {
        continue;
      }
      validateRequiredLimitValue(name, value, adapterLimits);
      if (ALIGNMENT_LIMIT_NAMES.has(name)) {
        limits[name] = Math.min(WEBGPU_DEFAULT_LIMITS[name], value);
      } else {
        limits[name] = Math.max(WEBGPU_DEFAULT_LIMITS[name], value);
      }
    }
    // https://gpuweb.github.io/gpuweb/#a-new-device: the per-stage storage
    // limits rise to the stage-specific ones, and a core device's
    // stage-specific limits then equal the per-stage limits.
    for (const [perStage, stages] of STAGE_STORAGE_LIMITS) {
      limits[perStage] = Math.max(limits[perStage], ...stages.map((stage) => limits[stage]));
      if (adapter.features.has(CORE_FEATURES_AND_LIMITS)) {
        for (const stage of stages) limits[stage] = limits[perStage];
      }
    }
    return Object.freeze(limits);
  }

  /* The native providers know only the per-stage storage limits: a required
   * stage-specific limit reaches them as its per-stage limit. */
  function nativeRequestDeviceDescriptor(descriptor) {
    const requiredLimits = descriptor?.requiredLimits;
    if (requiredLimits === undefined) return descriptor;
    const limits = { ...requiredLimits };
    for (const [perStage, stages] of STAGE_STORAGE_LIMITS) {
      const values = [perStage, ...stages].map((name) => limits[name]).filter((value) => value !== undefined);
      for (const stage of stages) delete limits[stage];
      if (values.length > 0) limits[perStage] = Math.max(...values);
    }
    return { ...descriptor, requiredLimits: limits };
  }

  function autoLayoutEntryKind(entry) {
    if (entry.buffer) return 'buffer';
    if (entry.sampler) return 'sampler';
    if (entry.texture) return 'texture';
    if (entry.storageTexture) return 'storageTexture';
    if (entry.externalTexture) return 'externalTexture';
    return 'unknown';
  }

  function autoLayoutEntriesCompatible(left, right) {
    const kind = autoLayoutEntryKind(left);
    if (kind !== autoLayoutEntryKind(right)) {
      return false;
    }
    switch (kind) {
      case 'buffer':
        return left.buffer.type === right.buffer.type;
      case 'sampler':
        return left.sampler.type === right.sampler.type;
      case 'texture':
        return left.texture.sampleType === right.texture.sampleType
          && left.texture.viewDimension === right.texture.viewDimension
          && left.texture.multisampled === right.texture.multisampled;
      case 'storageTexture':
        return left.storageTexture.access === right.storageTexture.access
          && left.storageTexture.format === right.storageTexture.format
          && left.storageTexture.viewDimension === right.storageTexture.viewDimension;
      case 'externalTexture':
        return true;
      default:
        return false;
    }
  }

  function mergeAutoLayoutEntriesByGroup(target, source, path) {
    if (!(source instanceof Map)) {
      return target;
    }
    for (const [group, entries] of source) {
      const merged = target.get(group) ?? [];
      const bindingToIndex = new Map(merged.map((entry, index) => [entry.binding, index]));
      for (const entry of entries) {
        const existingIndex = bindingToIndex.get(entry.binding);
        if (existingIndex === undefined) {
          const copy = { ...entry };
          merged.push(copy);
          bindingToIndex.set(copy.binding, merged.length - 1);
          continue;
        }
        const existing = merged[existingIndex];
        if (!autoLayoutEntriesCompatible(existing, entry)) {
          failValidation(path, `layout: "auto" resolves binding ${entry.binding} in group ${group} to incompatible declarations across shader stages`);
        }
        existing.visibility |= entry.visibility;
      }
      merged.sort((left, right) => left.binding - right.binding);
      target.set(group, merged);
    }
    return target;
  }

  // A render pipeline may omit the fragment stage (depth/stencil only) and may
  // leave color target slots null. fragmentTarget keeps the first non-null
  // target for backends that carry a single target.
  function normalizeFragmentStage(renderDescriptor, features, path) {
    if (renderDescriptor.fragment === undefined || renderDescriptor.fragment === null) {
      if (!renderDescriptor.depthStencil) {
        failValidation(path, 'descriptor needs a fragment stage or a depthStencil state');
      }
      return { fragmentModule: null, fragmentEntryPoint: null, fragmentConstants: null, fragmentTargets: [], fragmentTarget: null };
    }
    const fragment = assertObject(renderDescriptor.fragment, path, 'descriptor.fragment');
    const fragmentModule = assertLiveResource(fragment.module, path, 'GPUShaderModule');
    const targets = assertArray(fragment.targets ?? [], path, 'descriptor.fragment.targets').map((target, index) => {
      if (target === null || target === undefined) return null;
      const label = `descriptor.fragment.targets[${index}]`;
      const object = assertObject(target, path, label);
      return {
        ...object,
        format: normalizeTextureViewDescriptor(
          { format: assertNonEmptyString(object.format, path, `${label}.format`) },
          null,
          features,
          path,
        ).format,
      };
    });
    if (!targets.some(Boolean) && !renderDescriptor.depthStencil) {
      failValidation(path, 'descriptor.fragment.targets needs a color target when there is no depthStencil state');
    }
    return {
      fragmentModule,
      fragmentEntryPoint: inferShaderEntryPoint(fragment.module, fragment.entryPoint, 'fragment', path, 'descriptor.fragment.entryPoint'),
      fragmentConstants: fragment.constants ?? null,
      fragmentTargets: targets,
      fragmentTarget: targets.find(Boolean) ?? null,
    };
  }

  function renderAutoLayoutEntriesByGroup(renderDescriptor, path) {
    if (typeof backend.requireAutoLayoutEntriesFromNative !== 'function') {
      return null;
    }
    const layout = renderDescriptor.layout;
    if (layout !== 'auto' && layout !== undefined && layout !== null) {
      return null;
    }
    const vertexEntries = backend.requireAutoLayoutEntriesFromNative(
      renderDescriptor.vertexModule,
      globals.GPUShaderStage.VERTEX,
      path,
    );
    const fragmentEntries = renderDescriptor.fragmentModule
      ? backend.requireAutoLayoutEntriesFromNative(
        renderDescriptor.fragmentModule,
        globals.GPUShaderStage.FRAGMENT,
        path,
      )
      : null;
    if (!vertexEntries && !fragmentEntries) {
      return null;
    }
    const merged = new Map();
    mergeAutoLayoutEntriesByGroup(merged, vertexEntries, path);
    mergeAutoLayoutEntriesByGroup(merged, fragmentEntries, path);
    return merged;
  }

  // An invalid buffer keeps a real stand-in allocation so that mappedAtCreation
  // still yields a mapping; only mapping calls reach it.
  // A buffer created mappedAtCreation on a lost device has no allocation; its
  // mapping is host memory that unmap() discards.
  function lostDeviceMapping(buffer) {
    return buffer._lostDeviceMapping ?? null;
  }

  function bufferMappingNative(buffer, path) {
    if (buffer._invalid && buffer._native != null && !buffer._destroyed) {
      return buffer._native;
    }
    return assertLiveResource(buffer, path, 'GPUBuffer');
  }

  class DoeGPUBuffer {
    constructor(native, instance, size, usage, queue, owner) {
      this._native = native;
      this._instance = instance;
      this._queue = queue;
      this.size = size;
      this.usage = usage;
      this.label = '';
      // Map state lives in a cell the collected-buffer release also reads.
      this._mapCell = { state: 'unmapped' };
      initResource(this, 'GPUBuffer', owner);
      if (backend.initBufferState) {
        backend.initBufferState(this);
      }
      if (typeof backend.bufferRelease === 'function') {
        const mapCell = this._mapCell;
        releaseWhenCollected(this, native, (handle) => {
          // A buffer collected while mapped keeps its native: a mapped range
          // may still view its memory.
          if (mapCell.state !== 'mapped') backend.bufferRelease(handle);
        });
      }
    }

    get _mapState() {
      return this._mapCell.state;
    }

    set _mapState(state) {
      this._mapCell.state = state;
    }

    get mapState() {
      if (typeof backend.bufferGetMapState === 'function' && this._native != null) {
        const nativeState = backend.bufferGetMapState(this, this._native);
        if (typeof nativeState === 'string') {
          this._mapState = nativeState;
        }
      }
      return this._mapState ?? 'unmapped';
    }

    async mapAsync(mode, offset = 0, size = Math.max(0, this.size - offset)) {
      if (isDeviceLost(this._resourceOwner)) {
        throw abortError('GPUBuffer.mapAsync: the device is lost');
      }
      const native = bufferMappingNative(this, 'GPUBuffer.mapAsync');
      assertIntegerInRange(mode, 'GPUBuffer.mapAsync', 'mode', { min: 0, max: UINT32_MAX });
      assertIntegerInRange(offset, 'GPUBuffer.mapAsync', 'offset', { min: 0 });
      assertIntegerInRange(size, 'GPUBuffer.mapAsync', 'size', { min: 0 });
      if (offset + size > this.size) {
        failValidation('GPUBuffer.mapAsync', `mapped range ${offset}+${size} exceeds buffer size ${this.size}`);
      }
      const rejectValidationAsync = (message, finalState) => {
        const error = new GPUValidationError(`GPUBuffer.mapAsync: ${message}`);
        captureDeviceError(this._resourceOwner, 'validation', error);
        return Promise.resolve().then(() => {
          this._mapState = finalState;
          throw error;
        });
      };
      if (this._mapState === 'mapped' || this._mapState === 'pending') {
        return rejectValidationAsync('buffer is already mapped or mapping is pending', this._mapState);
      }
      this._mapState = 'pending';
      try {
        if (this._creationValidationError) {
          return rejectValidationAsync('buffer was created as a validation-error fallback object', 'unmapped');
        }
        if (mode === globals.GPUMapMode.READ) {
          if ((this.usage & globals.GPUBufferUsage.MAP_READ) === 0) {
            return rejectValidationAsync('buffer usage does not include MAP_READ', 'unmapped');
          }
        } else if (mode === globals.GPUMapMode.WRITE) {
          if ((this.usage & globals.GPUBufferUsage.MAP_WRITE) === 0) {
            return rejectValidationAsync('buffer usage does not include MAP_WRITE', 'unmapped');
          }
        } else {
          return rejectValidationAsync('mode must be GPUMapMode.READ or GPUMapMode.WRITE', 'unmapped');
        }
        await backend.bufferMapAsync(this, native, mode, offset, size);
        if (this._destroyed || this._mapState !== 'pending' || isDeviceLost(this._resourceOwner)) {
          this._mapState = 'unmapped';
          throw abortError('GPUBuffer.mapAsync: mapping was cancelled before it resolved');
        }
        this._mapState = 'mapped';
      } catch (error) {
        this._mapState = 'unmapped';
        throw error;
      }
    }

    getMappedRange(offset = 0, size = Math.max(0, this.size - offset)) {
      const hostMapping = lostDeviceMapping(this);
      if (hostMapping !== null) {
        assertIntegerInRange(offset, 'GPUBuffer.getMappedRange', 'offset', { min: 0 });
        assertIntegerInRange(size, 'GPUBuffer.getMappedRange', 'size', { min: 0 });
        if (offset + size > this.size) {
          failValidation('GPUBuffer.getMappedRange', `mapped range ${offset}+${size} exceeds buffer size ${this.size}`);
        }
        return hostMapping.slice(offset, offset + size);
      }
      const native = bufferMappingNative(this, 'GPUBuffer.getMappedRange');
      assertIntegerInRange(offset, 'GPUBuffer.getMappedRange', 'offset', { min: 0 });
      assertIntegerInRange(size, 'GPUBuffer.getMappedRange', 'size', { min: 0 });
      if (offset + size > this.size) {
        failValidation('GPUBuffer.getMappedRange', `mapped range ${offset}+${size} exceeds buffer size ${this.size}`);
      }
      return backend.bufferGetMappedRange(this, native, offset, size);
    }

    _readCopy(offset = 0, size = Math.max(0, this.size - offset)) {
      const native = assertLiveResource(this, 'GPUBuffer._readCopy', 'GPUBuffer');
      assertIntegerInRange(offset, 'GPUBuffer._readCopy', 'offset', { min: 0 });
      assertIntegerInRange(size, 'GPUBuffer._readCopy', 'size', { min: 0 });
      if (offset + size > this.size) {
        failValidation('GPUBuffer._readCopy', `mapped range ${offset}+${size} exceeds buffer size ${this.size}`);
      }
      if (typeof backend.bufferReadCopy === 'function') {
        return backend.bufferReadCopy(this, native, offset, size);
      }
      return backend.bufferGetMappedRange(this, native, offset, size).slice(0);
    }

    assertMappedPrefixF32(expected, count) {
      const native = assertLiveResource(this, 'GPUBuffer.assertMappedPrefixF32', 'GPUBuffer');
      assertIntegerInRange(count, 'GPUBuffer.assertMappedPrefixF32', 'count', { min: 0, max: UINT32_MAX });
      if (Array.isArray(expected)) {
        if (expected.length < count) {
          failValidation('GPUBuffer.assertMappedPrefixF32', `expected array must contain at least ${count} values`);
        }
        const actual = new Float32Array(this.getMappedRange(0, count * Float32Array.BYTES_PER_ELEMENT));
        for (let index = 0; index < count; index += 1) {
          if (actual[index] !== expected[index]) {
            failValidation(
              'GPUBuffer.assertMappedPrefixF32',
              `expected readback[${index}] === ${expected[index]}, got ${actual[index]}`,
            );
          }
        }
        return;
      }
      if (typeof expected !== 'number') {
        failValidation('GPUBuffer.assertMappedPrefixF32', 'expected must be a number or array of numbers');
      }
      if (typeof backend.bufferAssertMappedPrefixF32 === 'function') {
        return backend.bufferAssertMappedPrefixF32(this, native, expected, count);
      }
      const actual = new Float32Array(this.getMappedRange(0, count * Float32Array.BYTES_PER_ELEMENT));
      for (let index = 0; index < count; index += 1) {
        if (actual[index] !== expected) {
          failValidation(
            'GPUBuffer.assertMappedPrefixF32',
            `expected readback[${index}] === ${expected}, got ${actual[index]}`,
          );
        }
      }
    }

    _mapReadCopyUnmap(mode, offset = 0, size = Math.max(0, this.size - offset)) {
      const native = assertLiveResource(this, 'GPUBuffer._mapReadCopyUnmap', 'GPUBuffer');
      if (typeof backend.bufferMapReadCopyUnmap === 'function') {
        return backend.bufferMapReadCopyUnmap(this, native, mode, offset, size);
      }
      return null;
    }

    unmap() {
      this._lostDeviceMapping = null;
      if (this._destroyed || this._native == null || isDeviceLost(this._resourceOwner)) {
        this._mapState = 'unmapped';
        return;
      }
      backend.bufferUnmap(bufferMappingNative(this, 'GPUBuffer.unmap'), this);
      this._mapState = 'unmapped';
    }

    destroy() {
      destroyResource(this, (native) => backend.bufferDestroy(native, this));
      this._mapState = 'unmapped';
    }
  }

  class DoeGPUQueue {
    constructor(native, instance, device) {
      this._native = native;
      this._instance = instance;
      this._device = device;
      this._submittedWorkDone = null;
      this.label = '';
      initResource(this, 'GPUQueue', device);
      if (backend.initQueueState) {
        backend.initQueueState(this);
      }
      if (typeof backend.queueWriteBufferBatch === 'function') {
        Object.defineProperty(this, '__doeWriteBufferBatch', {
          value: (entries) => {
            const queueNative = assertLiveResource(this, 'GPUQueue.__doeWriteBufferBatch', 'GPUQueue');
            const normalized = assertArray(entries, 'GPUQueue.__doeWriteBufferBatch', 'entries')
              .map((entry, index) => {
                const item = normalizeWriteBufferBatchEntry(entry, index);
                return {
                  bufferNative: assertLiveResource(
                    item.buffer,
                    `GPUQueue.__doeWriteBufferBatch.entries[${index}]`,
                    'GPUBuffer',
                  ),
                  bufferOffset: item.bufferOffset,
                  view: item.view,
                };
              });
            return backend.queueWriteBufferBatch(this, queueNative, normalized);
          },
        });
      }
    }

    hasPendingSubmissions() {
      assertLiveResource(this, 'GPUQueue.hasPendingSubmissions', 'GPUQueue');
      return backend.queueHasPendingSubmissions(this);
    }

    markSubmittedWorkDone() {
      assertLiveResource(this, 'GPUQueue.markSubmittedWorkDone', 'GPUQueue');
      backend.queueMarkSubmittedWorkDone(this);
    }

    submit(commandBuffers) {
      const buffers = assertArray(commandBuffers, 'GPUQueue.submit', 'commandBuffers');
      if (isDeviceLost(this._device)) {
        for (const buffer of buffers) {
          if (buffer && typeof buffer === 'object') buffer._submitted = true;
        }
        return;
      }
      const native = assertLiveResource(this, 'GPUQueue.submit', 'GPUQueue');
      if (buffers.length === 0) {
        return;
      }
      const invalid = buffers.find(isInvalidResource);
      // A command buffer that names a destroyed texture fails here (WebGPU
      // validates destroyed resources at submit).
      const destroyedUse = buffers.find((buffer) => buffer?._submitError != null);
      if (invalid !== undefined || destroyedUse !== undefined) {
        // A failed submit still consumes every command buffer it names.
        for (const buffer of buffers) {
          buffer.destroy?.();
          buffer._submitted = true;
        }
        if (invalid !== undefined) reportInvalidUse(this._device, 'GPUQueue.submit', invalid);
        else reportValidationError(this._device, `GPUQueue.submit: ${destroyedUse._submitError}`);
        return;
      }
      return backend.queueSubmit(this, native, buffers);
    }

    writeBuffer(buffer, bufferOffset, data, dataOffset = 0, size) {
      if (isDeviceLost(this._device)) {
        return;
      }
      const native = assertLiveResource(this, 'GPUQueue.writeBuffer', 'GPUQueue');
      if (isInvalidResource(buffer)) {
        reportInvalidUse(this._device, 'GPUQueue.writeBuffer', buffer);
        return;
      }
      const bufferNative = assertLiveResource(buffer, 'GPUQueue.writeBuffer', 'GPUBuffer');
      assertIntegerInRange(bufferOffset, 'GPUQueue.writeBuffer', 'bufferOffset', { min: 0 });
      const view = fastDefaultWriteBufferView(data, dataOffset, size)
        ?? validateWriteBufferInput(data, dataOffset, size, 'GPUQueue.writeBuffer');
      if (bufferOffset + view.byteLength > buffer.size) {
        // A device-timeline validation error: reported, or thrown unobserved.
        reportValidationError(this._device, `GPUQueue.writeBuffer: write range ${bufferOffset}+${view.byteLength} exceeds buffer size ${buffer.size}`);
        return;
      }
      return backend.queueWriteBuffer(this, native, bufferNative, bufferOffset, view);
    }

    writeTexture(destination, data, dataLayout, size) {
      if (isDeviceLost(this._device)) {
        return;
      }
      const native = assertLiveResource(this, 'GPUQueue.writeTexture', 'GPUQueue');
      const destinationObject = assertObject(destination, 'GPUQueue.writeTexture', 'destination');
      const layoutObject = assertObject(dataLayout, 'GPUQueue.writeTexture', 'dataLayout');
      const sizeObject = normalizeExtent3D(size, 'GPUQueue.writeTexture', 'size');
      const view = validateWriteBufferInput(data, 0, undefined, 'GPUQueue.writeTexture');
      if (isInvalidResource(destinationObject.texture)) {
        reportInvalidUse(this._device, 'GPUQueue.writeTexture', destinationObject.texture);
        return;
      }
      if (destinationObject.texture?._resourceLabel === 'GPUTexture' && destinationObject.texture._destroyed) {
        reportValidationError(this._device, 'GPUQueue.writeTexture: destination.texture was destroyed');
        return;
      }
      return backend.queueWriteTexture(
        this,
        native,
        {
          texture: assertLiveResource(destinationObject.texture, 'GPUQueue.writeTexture', 'GPUTexture'),
          mipLevel: destinationObject.mipLevel ?? 0,
          origin: normalizeOrigin3D(destinationObject.origin, 'GPUQueue.writeTexture', 'destination.origin'),
          aspect: destinationObject.aspect,
        },
        view,
        {
          offset: layoutObject.offset ?? 0,
          bytesPerRow: layoutObject.bytesPerRow,
          rowsPerImage: layoutObject.rowsPerImage,
        },
        {
          width: sizeObject.width,
          height: sizeObject.height,
          depthOrArrayLayers: sizeObject.depthOrArrayLayers ?? 1,
        },
      );
    }

    async onSubmittedWorkDone() {
      if (isDeviceLost(this._device)) {
        await this._submittedWorkDone;
        return;
      }
      const native = assertLiveResource(this, 'GPUQueue.onSubmittedWorkDone', 'GPUQueue');
      if (!this.hasPendingSubmissions()) {
        return;
      }
      const completion = backend.queueOnSubmittedWorkDone(this, native);
      this._submittedWorkDone = completion;
      await completion;
      if (!this._device?._destroyed) this.markSubmittedWorkDone();
    }

    copyExternalImageToTexture(source, destination, copySize) {
      if (isDeviceLost(this._device)) {
        return;
      }
      const native = assertLiveResource(this, 'GPUQueue.copyExternalImageToTexture', 'GPUQueue');
      const sourceObject = assertObject(source, 'GPUQueue.copyExternalImageToTexture', 'source');
      const destinationObject = assertObject(destination, 'GPUQueue.copyExternalImageToTexture', 'destination');
      const sizeObject = assertObject(copySize, 'GPUQueue.copyExternalImageToTexture', 'copySize');
      if (typeof backend.queueCopyExternalImageToTexture !== 'function') {
        failValidation(
          'GPUQueue.copyExternalImageToTexture',
          'copyExternalImageToTexture is not supported on this package surface',
        );
      }
      if (sourceObject.source == null) {
        failValidation('GPUQueue.copyExternalImageToTexture', 'source.source is required');
      }
      const texture = assertLiveResource(
        destinationObject.texture,
        'GPUQueue.copyExternalImageToTexture',
        'GPUTexture',
      );
      const normalizedSource = {
        ...sourceObject,
        origin: normalizeOrigin2DForExternalCopy(
          sourceObject.origin,
          'GPUQueue.copyExternalImageToTexture(source.origin)',
        ),
      };
      const normalizedDestination = {
        ...destinationObject,
        texture,
        mipLevel: assertIntegerInRange(
          destinationObject.mipLevel ?? 0,
          'GPUQueue.copyExternalImageToTexture',
          'destination.mipLevel',
          { min: 0, max: UINT32_MAX },
        ),
        origin: normalizeOrigin3DForExternalCopy(
          destinationObject.origin,
          'GPUQueue.copyExternalImageToTexture(destination.origin)',
        ),
      };
      if (sourceObject.flipY !== undefined) {
        normalizedSource.flipY = assertBoolean(
          sourceObject.flipY,
          'GPUQueue.copyExternalImageToTexture',
          'source.flipY',
        );
      }
      if (destinationObject.premultipliedAlpha !== undefined) {
        normalizedDestination.premultipliedAlpha = assertBoolean(
          destinationObject.premultipliedAlpha,
          'GPUQueue.copyExternalImageToTexture',
          'destination.premultipliedAlpha',
        );
      }
      if (destinationObject.colorSpace !== undefined) {
        assertNonEmptyString(
          destinationObject.colorSpace,
          'GPUQueue.copyExternalImageToTexture',
          'destination.colorSpace',
        );
      }
      const normalizedSize = {
        width: assertIntegerInRange(sizeObject.width, 'GPUQueue.copyExternalImageToTexture', 'copySize.width', { min: 1, max: UINT32_MAX }),
        height: assertIntegerInRange(sizeObject.height, 'GPUQueue.copyExternalImageToTexture', 'copySize.height', { min: 1, max: UINT32_MAX }),
        depthOrArrayLayers: assertIntegerInRange(
          sizeObject.depthOrArrayLayers ?? 1,
          'GPUQueue.copyExternalImageToTexture',
          'copySize.depthOrArrayLayers',
          { min: 1, max: UINT32_MAX },
        ),
      };
      return backend.queueCopyExternalImageToTexture(
        this,
        native,
        normalizedSource,
        normalizedDestination,
        normalizedSize,
      );
    }
  }

  class DoeGPUTexture {
    constructor(native, owner, meta) {
      this._native = native;
      this.width = meta?.width ?? 1;
      this.height = meta?.height ?? 1;
      this.depthOrArrayLayers = meta?.depthOrArrayLayers ?? 1;
      this.mipLevelCount = meta?.mipLevelCount ?? 1;
      this.sampleCount = meta?.sampleCount ?? 1;
      this.dimension = meta?.dimension ?? '2d';
      this.format = meta?.format ?? 'rgba8unorm';
      this.usage = meta?.usage ?? 0;
      this.textureBindingViewDimension = meta?.textureBindingViewDimension;
      this.viewFormats = Array.isArray(meta?.viewFormats) ? meta.viewFormats : [];
      this._externallyOwned = meta?.externallyOwned === true;
      this.label = '';
      initResource(this, 'GPUTexture', owner);
      if (backend.initTextureState) {
        backend.initTextureState(this);
      }
      // A texture collected without destroy() drops its native reference; a
      // canvas owns the native of its current texture.
      if (!this._externallyOwned) releaseWhenCollected(this, native, backend.textureRelease);
    }

    createView(descriptor) {
      return createReportingErrors(
        this._resourceOwner,
        'GPUTexture.createView',
        () => this._createViewOrNull(descriptor),
        () => markInvalid(new DoeGPUTextureView(null, this), labelOf(descriptor)),
      );
    }

    _createViewOrNull(descriptor) {
      // A destroyed texture still makes views (WebGPU): it has no native
      // handle, so the view has none either and any submission using it fails.
      const destroyed = this._destroyed && !this._invalid;
      const texture = destroyed ? null : assertLiveResource(this, 'GPUTexture.createView', 'GPUTexture');
      const features = this._resourceOwner?.features ?? this._features ?? null;
      const viewDescriptor = descriptor == null ? {} : assertObject(descriptor, 'GPUTexture.createView', 'descriptor');
      const baseMipLevel = assertIntegerInRange(viewDescriptor.baseMipLevel ?? 0, 'GPUTexture.createView', 'descriptor.baseMipLevel', { min: 0, max: UINT32_MAX });
      const baseArrayLayer = assertIntegerInRange(viewDescriptor.baseArrayLayer ?? 0, 'GPUTexture.createView', 'descriptor.baseArrayLayer', { min: 0, max: UINT32_MAX });
      const mipLevelCount = assertIntegerInRange(viewDescriptor.mipLevelCount ?? Math.max(1, this.mipLevelCount - baseMipLevel), 'GPUTexture.createView', 'descriptor.mipLevelCount', { min: 1, max: UINT32_MAX });
      // https://gpuweb.github.io/gpuweb/#abstract-opdef-resolving-gputextureviewdescriptor-defaults
      const aspect = viewDescriptor.aspect ?? 'all';
      const aspectFormat = textureAspectFormat(this.format, aspect);
      const dimension = viewDescriptor.dimension ?? this.textureBindingViewDimension ??
        (this.dimension === '2d' && this.depthOrArrayLayers > 1 ? '2d-array' : this.dimension);
      const defaultArrayLayerCount = dimension === '2d-array' || dimension === 'cube-array'
        ? Math.max(1, this.depthOrArrayLayers - baseArrayLayer)
        : (dimension === 'cube' ? 6 : 1);
      const arrayLayerCount = assertIntegerInRange(viewDescriptor.arrayLayerCount ?? defaultArrayLayerCount, 'GPUTexture.createView', 'descriptor.arrayLayerCount', { min: 1, max: UINT32_MAX });
      // A feature-gated view format throws TypeError before any compatibility check.
      if (viewDescriptor.format !== undefined) {
        normalizeTextureFormat(viewDescriptor.format, 'GPUTexture.createView', 'descriptor.format', features);
      }
      const format = viewDescriptor.format ?? aspectFormat ?? this.format;
      if (aspect === 'all' ? format !== this.format && !this.viewFormats.includes(format) : format !== aspectFormat) {
        failValidation('GPUTexture.createView', aspect === 'all'
          ? 'descriptor.format must match texture.format or one of texture.viewFormats'
          : `descriptor.format must be the ${aspect} format of ${this.format}`);
      }
      // swizzle is a WebIDL DOMString (1111 converts to "1111"); a value outside
      // the four-character [rgba01] grammar is a content-timeline TypeError.
      const swizzle = viewDescriptor.swizzle === undefined ? undefined : String(viewDescriptor.swizzle);
      if (swizzle !== undefined && !/^[rgba01]{4}$/.test(swizzle)) {
        throw new TypeError('GPUTexture.createView: descriptor.swizzle must be a 4-character string using r, g, b, a, 0, or 1');
      }
      const normalizedDescriptor = normalizeTextureViewDescriptor({
        ...viewDescriptor,
        ...(swizzle === undefined ? {} : { swizzle }),
        format,
        dimension,
        baseMipLevel,
        mipLevelCount,
        baseArrayLayer,
        arrayLayerCount,
        aspect,
        usage: viewDescriptor.usage ?? this.usage,
      }, this, features, 'GPUTexture.createView');
      const view = destroyed ? null : backend.textureCreateView(this, texture, normalizedDescriptor);
      if (view === null && !destroyed) {
        return null;
      }
      const tv = new DoeGPUTextureView(view, this);
      tv._dimension = dimension;
      if (destroyed) tv._textureDestroyed = true;
      // A canvas texture invalidates its views when the frame ends; other
      // textures hold no reference to their views, which stay collectable.
      if (this._externallyOwned) {
        if (!this._childViews) {
          this._childViews = new Set();
        }
        this._childViews.add(tv);
      }
      tv.label = normalizedDescriptor.label ?? '';
      return tv;
    }

    destroy() {
      destroyResource(this, (native) => backend.textureDestroy(native, this));
    }
  }

  // Views, samplers, render pipelines, shader modules and query sets have no
  // (or a non-releasing) destroy(): a collected one drops its native reference.
  // Native owners retain what they record or bind, so a collected object never
  // frees state a bind group, pass or command buffer still uses.
  class DoeGPUTextureView {
    constructor(native, owner) {
      this._native = native;
      this.label = '';
      initResource(this, 'GPUTextureView', owner);
      releaseWhenCollected(this, native, backend.textureViewRelease);
    }
  }

  class DoeGPUSampler {
    constructor(native, owner) {
      this._native = native;
      this.label = '';
      initResource(this, 'GPUSampler', owner);
      releaseWhenCollected(this, native, backend.samplerRelease);
    }
  }

  // getBindGroupLayout on an invalid pipeline, or past maxBindGroups, is a
  // validation error that returns an invalid layout.
  function pipelineBindGroupLayout(pipeline, index, label) {
    const path = `${label}.getBindGroupLayout`;
    const device = pipeline._device;
    return createReportingErrors(device, path, () => {
      assertLiveResource(pipeline, path, label);
      assertIntegerInRange(index, path, 'index', { min: 0, max: UINT32_MAX });
      const maxBindGroups = device?.limits?.maxBindGroups ?? WEBGPU_DEFAULT_LIMITS.maxBindGroups;
      if (index >= maxBindGroups) {
        failValidation(path, `index ${index} is not below device.limits.maxBindGroups (${maxBindGroups})`);
      }
      return pipeline._getBindGroupLayoutOrNull(index);
    }, () => markInvalid(new DoeGPUBindGroupLayout(null, device)));
  }

  class DoeGPURenderPipeline {
    constructor(native, owner, explicitLayout, autoLayoutEntriesByGroup) {
      this._native = native;
      this._device = owner;
      this._explicitLayout = explicitLayout;
      this._autoLayoutEntriesByGroup = autoLayoutEntriesByGroup;
      this._cachedLayouts = new Map();
      this.label = '';
      initResource(this, 'GPURenderPipeline', owner);
      releaseWhenCollected(this, native, backend.renderPipelineRelease);
    }

    getBindGroupLayout(index) {
      return pipelineBindGroupLayout(this, index, 'GPURenderPipeline');
    }

    _getBindGroupLayoutOrNull(index) {
      if (this._cachedLayouts.has(index)) {
        return this._cachedLayouts.get(index);
      }
      // The provider's own layout comes first: its default layout follows the
      // entry points' shader types. A provider that reports none (null) gets
      // a layout built from module reflection, adopted by this pipeline.
      let layout = backend.renderPipelineGetBindGroupLayout(this, index, classes);
      if (layout == null && this._autoLayoutEntriesByGroup) {
        const entries = this._autoLayoutEntriesByGroup.get(index) ?? [];
        layout = this._device.createBindGroupLayout({ entries });
        if (typeof backend.pipelineAdoptAutoBindGroupLayout === 'function') {
          backend.pipelineAdoptAutoBindGroupLayout(this, true, index, layout);
        }
      }
      if (layout == null) {
        throw new Error('[doe-gpu] renderPipelineGetBindGroupLayout failed');
      }
      this._cachedLayouts.set(index, layout);
      return layout;
    }
  }

  class DoeGPUShaderModule {
    constructor(native, code, owner) {
      this._native = native;
      this._code = code;
      this.label = '';
      initResource(this, 'GPUShaderModule', owner);
      releaseWhenCollected(this, native, backend.shaderModuleDestroy);
    }

    // Compilation info stays available for a module the provider rejected.
    async getCompilationInfo() {
      if (this._native == null && this._invalid) {
        return { messages: [] };
      }
      const native = this._invalid ? this._native : assertLiveResource(this, 'GPUShaderModule.getCompilationInfo', 'GPUShaderModule');
      if (typeof backend.shaderModuleGetCompilationInfo === 'function') {
        return backend.shaderModuleGetCompilationInfo(this, native);
      }
      return { messages: [] };
    }

    destroy() {
      if (typeof backend.shaderModuleDestroy !== 'function') {
        return;
      }
      destroyResource(this, (native) => backend.shaderModuleDestroy(native, this));
    }
  }

  class DoeGPUComputePipeline {
    constructor(native, device, explicitLayout, autoLayoutEntriesByGroup) {
      this._native = native;
      this._device = device;
      this._explicitLayout = explicitLayout;
      this._autoLayoutEntriesByGroup = autoLayoutEntriesByGroup;
      this._cachedLayouts = new Map();
      this.label = '';
      initResource(this, 'GPUComputePipeline', device, backend.computePipelineRelease);
    }

    getBindGroupLayout(index) {
      return pipelineBindGroupLayout(this, index, 'GPUComputePipeline');
    }

    _getBindGroupLayoutOrNull(index) {
      if (this._cachedLayouts.has(index)) {
        return this._cachedLayouts.get(index);
      }
      const layout = backend.computePipelineGetBindGroupLayout(this, index, classes);
      this._cachedLayouts.set(index, layout);
      return layout;
    }
  }

  class DoeGPUBindGroupLayout {
    constructor(native, owner) {
      this._native = native;
      this.label = '';
      initResource(this, 'GPUBindGroupLayout', owner, backend.bindGroupLayoutRelease);
    }
  }

  class DoeGPUBindGroup {
    constructor(native, owner) {
      this._native = native;
      this.label = '';
      initResource(this, 'GPUBindGroup', owner, backend.bindGroupRelease);
    }
  }

  class DoeGPUPipelineLayout {
    constructor(native, owner) {
      this._native = native;
      this.label = '';
      initResource(this, 'GPUPipelineLayout', owner, backend.pipelineLayoutRelease);
    }
  }

  class DoeGPUQuerySet {
    constructor(native, type, count, owner) {
      this._native = native;
      this.type = type;
      this.count = count;
      this.label = '';
      initResource(this, 'GPUQuerySet', owner);
      // No query-set release export exists; destroy() also releases.
      releaseWhenCollected(this, native, backend.querySetDestroy);
    }

    destroy() {
      destroyResource(this, (native) => backend.querySetDestroy(native));
    }
  }

  class DoeGPUDevice {
    constructor(native, instance, inheritedLimits = null, inheritedFeatures = null) {
      this._native = native;
      this._instance = instance;
      this._adapter = null;
      this._adapterInfo = null;
      this._features = inheritedFeatures ?? null;
      this._limits = inheritedLimits ?? null;
      this._onuncapturederror = null;
      this._eventListeners = new Map();
      this._errorScopes = [];
      this._errorScopeDepth = 0;
      this.label = '';
      initResource(this, 'GPUDevice');
      createDeviceLostState(this);
      if (typeof backend.initDeviceState === 'function') {
        backend.initDeviceState(this);
      }
      this.queue = new DoeGPUQueue(backend.deviceGetQueue(native), instance, this);
    }

    get limits() {
      if (this._limits === null) {
        this._limits = backend.deviceLimits(assertLiveResource(this, 'GPUDevice.limits', 'GPUDevice'));
      }
      return this._limits;
    }

    get features() {
      if (this._features === null) {
        this._features = backend.deviceFeatures(assertLiveResource(this, 'GPUDevice.features', 'GPUDevice'));
      }
      return this._features;
    }

    addEventListener(type, listener, options) {
      addDeviceEventListener(this, type, listener, options);
    }

    removeEventListener(type, listener) {
      removeDeviceEventListener(this, type, listener);
    }

    // Delivers the provider errors logged since the last take, in order.
    _drainProviderErrors() {
      if (this._providerErrorLog !== true || this._native == null) {
        return;
      }
      const mark = backend.providerErrorMark();
      if (mark === this._providerErrorSeen) {
        return;
      }
      this._providerErrorSeen = mark;
      for (const error of backend.deviceTakeProviderErrors(this)) {
        deliverDeviceError(this, error.type, error);
      }
    }

    // Takes, without delivering, what the provider reported since `mark`
    // (a providerErrorMark() read right after a drain).
    _takeProviderErrorsSince(mark) {
      if (this._native == null) {
        return [];
      }
      const now = backend.providerErrorMark();
      if (now === mark) {
        return [];
      }
      this._providerErrorSeen = now;
      return backend.deviceTakeProviderErrors(this);
    }

    _collectErrors(run) {
      this._drainProviderErrors();
      const mark = this._providerErrorLog === true ? backend.providerErrorMark() : null;
      const outer = this._errorCollection;
      const collected = [];
      this._errorCollection = collected;
      let result;
      try {
        result = run();
      } finally {
        if (mark !== null) {
          for (const error of this._takeProviderErrorsSince(mark)) {
            collected.push({ filter: error.type, error });
          }
        }
        this._errorCollection = outer;
      }
      return { result, errors: collected };
    }

    get lost() {
      if (typeof backend.deviceGetLost === 'function' && !this._destroyed) {
        const native = assertLiveResource(this, 'GPUDevice.lost', 'GPUDevice');
        return backend.deviceGetLost(this, native);
      }
      createDeviceLostState(this);
      return this._lost;
    }

    get adapterInfo() {
      if (this._adapterInfo != null) {
        return this._adapterInfo;
      }
      if (this._adapter?.info) {
        this._adapterInfo = this._adapter.info;
        return this._adapterInfo;
      }
      if (typeof backend.deviceGetAdapterInfo === 'function') {
        const native = assertLiveResource(this, 'GPUDevice.adapterInfo', 'GPUDevice');
        this._adapterInfo = backend.deviceGetAdapterInfo(this, native);
        return this._adapterInfo;
      }
      this._adapterInfo = EMPTY_ADAPTER_INFO;
      return this._adapterInfo;
    }

    // A device-timeline validation error the package detects itself: the
    // innermost validation scope captures it, or it is uncaptured.
    _reportValidationError(message) {
      captureDeviceError(this, 'validation', new GPUValidationError(message));
    }

    pushErrorScope(filter) {
      const encodedFilter = normalizeErrorFilter(filter, 'GPUDevice.pushErrorScope');
      if (isDeviceLost(this)) {
        this._errorScopes.push({ filter, error: null });
        this._errorScopeDepth = this._errorScopes.length;
        return;
      }
      const native = assertLiveResource(this, 'GPUDevice.pushErrorScope', 'GPUDevice');
      if (this._providerErrorLog === true) {
        this._drainProviderErrors();
      } else if (typeof backend.devicePushErrorScope === 'function') {
        backend.devicePushErrorScope(this, native, filter, encodedFilter);
      }
      this._errorScopes.push({ filter, error: null });
      this._errorScopeDepth = this._errorScopes.length;
    }

    // A lost device resolves every pop with null, even past the stack.
    popErrorScope() {
      if (isDeviceLost(this)) {
        this._errorScopes.pop();
        this._errorScopeDepth = this._errorScopes.length;
        return Promise.resolve(null);
      }
      const native = assertLiveResource(this, 'GPUDevice.popErrorScope', 'GPUDevice');
      if (!Array.isArray(this._errorScopes) || this._errorScopes.length <= 0) {
        return Promise.reject(operationError('GPUDevice.popErrorScope: no active error scope'));
      }
      if (this._providerErrorLog === true) {
        this._drainProviderErrors();
        const scope = this._errorScopes.pop();
        this._errorScopeDepth = this._errorScopes.length;
        return Promise.resolve(scope.error);
      }
      const scope = this._errorScopes.pop();
      this._errorScopeDepth = this._errorScopes.length;
      const nativePop = typeof backend.devicePopErrorScope === 'function'
        ? backend.devicePopErrorScope(this, native)
        : Promise.resolve(null);
      return Promise.resolve(nativePop).then(nativeError => scope.error ?? nativeError ?? null);
    }

    get onuncapturederror() {
      if (!this._destroyed && typeof backend.deviceGetOnUncapturedError === 'function') {
        const native = assertLiveResource(this, 'GPUDevice.onuncapturederror', 'GPUDevice');
        return backend.deviceGetOnUncapturedError(this, native);
      }
      return this._onuncapturederror;
    }

    set onuncapturederror(handler) {
      if (handler !== null && handler !== undefined && typeof handler !== 'function') {
        failValidation('GPUDevice.onuncapturederror', 'handler must be a function or null');
      }
      this._onuncapturederror = handler ?? null;
      if (!this._destroyed && typeof backend.deviceSetOnUncapturedError === 'function') {
        const native = assertLiveResource(this, 'GPUDevice.onuncapturederror', 'GPUDevice');
        backend.deviceSetOnUncapturedError(this, native, handler ?? null);
        return;
      }
    }

    createBuffer(descriptor) {
      const path = 'GPUDevice.createBuffer';
      return createReportingErrors(this, path, () => {
        const validated = assertBufferDescriptor(descriptor, path);
        if (validated.size > (this.limits?.maxBufferSize ?? WEBGPU_DEFAULT_LIMITS.maxBufferSize)) {
          failValidation(path, 'descriptor.size exceeds device.limits.maxBufferSize');
        }
        const native = backend.deviceCreateBuffer(this, validated);
        if (native === null) {
          return null;
        }
        const buffer = new DoeGPUBuffer(native, this._instance, validated.size, validated.usage, this.queue, this);
        buffer.label = descriptor?.label ?? '';
        if (validated.mappedAtCreation && typeof backend.bufferMarkMappedAtCreation === 'function') {
          backend.bufferMarkMappedAtCreation(buffer);
          buffer._mapState = 'mapped';
        }
        return buffer;
      }, () => this._createErrorScopeFallbackBuffer(
        labelOf(descriptor),
        descriptor?.size,
        Boolean(descriptor?.mappedAtCreation),
      ));
    }

    _createErrorScopeFallbackBuffer(label = '', requestedSize = 4, mappedAtCreation = false) {
      const fallbackSize = Number.isSafeInteger(requestedSize)
        && requestedSize >= 0
        && requestedSize <= (this.limits?.maxBufferSize ?? WEBGPU_DEFAULT_LIMITS.maxBufferSize)
        ? requestedSize
        : 4;
      const descriptor = {
        label,
        size: fallbackSize,
        usage: globals.GPUBufferUsage.COPY_SRC | globals.GPUBufferUsage.MAP_WRITE,
        mappedAtCreation,
      };
      // A lost device gets no stand-in allocation.
      const native = this._destroyed ? null : backend.deviceCreateBuffer(this, descriptor);
      const buffer = markInvalid(
        new DoeGPUBuffer(native, this._instance, descriptor.size, descriptor.usage, this.queue, this),
        label,
      );
      buffer._creationValidationError = true;
      if (native !== null && mappedAtCreation && typeof backend.bufferMarkMappedAtCreation === 'function') {
        backend.bufferMarkMappedAtCreation(buffer);
        buffer._mapState = 'mapped';
      } else if (native === null && mappedAtCreation) {
        buffer._lostDeviceMapping = new ArrayBuffer(Number.isSafeInteger(requestedSize) && requestedSize >= 0 ? requestedSize : fallbackSize);
        buffer._mapState = 'mapped';
      }
      return buffer;
    }

    createShaderModule(descriptor) {
      const path = 'GPUDevice.createShaderModule';
      return createReportingErrors(this, path, () => {
        const objectDescriptor = assertObject(descriptor, path, 'descriptor');
        const code = objectDescriptor.code ?? objectDescriptor.source;
        assertNonEmptyString(code, path, 'descriptor.code');
        if (backend.preflightShaderSourceOnCreate !== false) {
          const preflight = backend.preflightShaderSource(code);
          if (!preflight.ok) {
            shaderCheckFailure(path, preflight);
          }
        }
        const hints = objectDescriptor.compilationHints ?? null;
        const native = backend.deviceCreateShaderModule(this, code, hints, objectDescriptor.label ?? null);
        if (native === null) {
          return null;
        }
        const module = new DoeGPUShaderModule(native, code, this);
        module.label = objectDescriptor.label ?? '';
        return module;
      }, () => markInvalid(
        new DoeGPUShaderModule(null, typeof descriptor?.code === 'string' ? descriptor.code : '', this),
        labelOf(descriptor),
      ));
    }

    // Validates a compute pipeline descriptor and returns how to create it;
    // wrap(native) returns the pipeline, or null for a provider rejection.
    _computePipelineRequest(descriptor, path) {
      const pipelineDescriptor = assertObject(descriptor, path, 'descriptor');
      const compute = assertObject(pipelineDescriptor.compute, path, 'descriptor.compute');
      const shader = compute.module;
      const shaderNative = assertLiveResource(shader, path, 'GPUShaderModule');
      const entryPoint = inferShaderEntryPoint(shader, compute.entryPoint, 'compute', path, 'descriptor.compute.entryPoint');
      assertShaderEntryPoint(shader, entryPoint, path);
      const layout = pipelineDescriptor.layout === 'auto' || pipelineDescriptor.layout === undefined
        ? null
        : pipelineDescriptor.layout;
      if (layout !== null) {
        assertLiveResource(layout, path, 'GPUPipelineLayout');
      }
      const autoLayoutEntriesByGroup = layout
        ? null
        : backend.requireAutoLayoutEntriesFromNative(shader, globals.GPUShaderStage.COMPUTE, path, entryPoint);
      const constants = compute.constants ?? null;
      const label = pipelineDescriptor.label || undefined;
      const layoutNative = layout?._native ?? null;
      return {
        create: () => backend.deviceCreateComputePipeline(this, shaderNative, entryPoint, layoutNative, constants, label),
        createAsync: typeof backend.deviceCreateComputePipelineAsync === 'function'
          ? () => backend.deviceCreateComputePipelineAsync(this, shaderNative, entryPoint, layoutNative, constants, label)
          : null,
        wrap: (native) => {
          if (native === null) {
            return null;
          }
          const pipeline = new DoeGPUComputePipeline(native, this, layout, autoLayoutEntriesByGroup);
          pipeline.label = pipelineDescriptor.label ?? '';
          return pipeline;
        },
      };
    }

    createComputePipeline(descriptor) {
      const path = 'GPUDevice.createComputePipeline';
      return createReportingErrors(this, path, () => {
        const request = this._computePipelineRequest(descriptor, path);
        return request.wrap(shouldRouteDeviceError(this)
          ? request.create()
          : createWithProviderMessage(this, path, request.create));
      }, () => markInvalid(new DoeGPUComputePipeline(null, this, null, null), labelOf(descriptor)), wrapPipelineError);
    }

    // Rejects with GPUPipelineError for every failure; no error reaches a scope.
    async createComputePipelineAsync(descriptor) {
      const path = 'GPUDevice.createComputePipelineAsync';
      if (isDeviceLost(this)) {
        return markInvalid(new DoeGPUComputePipeline(null, this, null, null), labelOf(descriptor));
      }
      try {
        const request = this._computePipelineRequest(descriptor, path);
        const native = request.createAsync
          ? await request.createAsync()
          : createWithProviderMessage(this, path, request.create);
        const pipeline = request.wrap(native);
        if (pipeline === null) {
          throw providerRejection(path);
        }
        return pipeline;
      } catch (error) {
        throw wrapPipelineError(error);
      }
    }

    createBindGroupLayout(descriptor) {
      const path = 'GPUDevice.createBindGroupLayout';
      return createReportingErrors(this, path, () => {
        const layoutDescriptor = assertObject(descriptor, path, 'descriptor');
        // Every layout goes through complete entry descriptors: layout
        // compatibility compares each entry's type, visibility and sizes.
        const entries = assertArray(layoutDescriptor.entries ?? [], path, 'descriptor.entries')
          .map((entry, index) => normalizeBindGroupLayoutEntry(entry, index, path, this.features));
        const native = backend.deviceCreateBindGroupLayout(this, entries, layoutDescriptor.label || undefined);
        if (native === null) {
          return null;
        }
        const bgl = new DoeGPUBindGroupLayout(native, this);
        bgl.label = layoutDescriptor.label ?? '';
        return bgl;
      }, () => markInvalid(new DoeGPUBindGroupLayout(null, this), labelOf(descriptor)));
    }

    createBindGroup(descriptor) {
      const path = 'GPUDevice.createBindGroup';
      return createReportingErrors(this, path, () => {
        const bindGroupDescriptor = assertObject(descriptor, path, 'descriptor');
        const layoutNative = assertLiveResource(bindGroupDescriptor.layout, path, 'GPUBindGroupLayout');
        // A view made after its texture's destroy(), a destroyed GPUTexture or
        // a destroyed GPUBuffer has no native handle; the bind group is still
        // valid (WebGPU validates destroyed resources at submit) and any
        // command buffer that uses it fails at submit.
        const destroyedKind = Array.isArray(bindGroupDescriptor.entries)
          ? bindGroupDescriptor.entries.map((entry) => destroyedBindingKind(entry?.resource)).find((kind) => kind !== null) ?? null
          : null;
        if (destroyedKind !== null) {
          const bg = new DoeGPUBindGroup(null, this);
          bg._textureDestroyed = true;
          bg._destroyedResourceKind = destroyedKind;
          bg.label = bindGroupDescriptor.label ?? '';
          return bg;
        }
        let native = tryCreateBufferBindGroupFast4(backend, this, layoutNative, bindGroupDescriptor);
        if (native === FAST_PATH_NOT_APPLIED) {
          const entries = assertArray(bindGroupDescriptor.entries ?? [], path, 'descriptor.entries')
            .map((entry, index) => {
              const binding = assertObject(entry, path, `descriptor.entries[${index}]`);
              const resource = assertBindGroupResource(binding.resource, path);
              const normalized = {
                binding: assertIntegerInRange(binding.binding, path, `descriptor.entries[${index}].binding`, { min: 0, max: UINT32_MAX }),
                buffer: resource.buffer,
                sampler: resource.sampler,
                textureView: resource.textureView,
                externalTexture: resource.externalTexture,
                offset: resource.offset ?? 0,
              };
              if (resource.size !== undefined) {
                normalized.size = resource.size;
              }
              return normalized;
            });
          native = backend.deviceCreateBindGroup(this, layoutNative, entries, bindGroupDescriptor.label || undefined);
        }
        if (native === null) {
          return null;
        }
        const bg = new DoeGPUBindGroup(native, this);
        bg.label = bindGroupDescriptor.label ?? '';
        return bg;
      }, () => markInvalid(new DoeGPUBindGroup(null, this), labelOf(descriptor)));
    }

    createPipelineLayout(descriptor) {
      const path = 'GPUDevice.createPipelineLayout';
      return createReportingErrors(this, path, () => {
        const layoutDescriptor = assertObject(descriptor, path, 'descriptor');
        const layouts = assertArray(layoutDescriptor.bindGroupLayouts ?? [], path, 'descriptor.bindGroupLayouts')
          // sequence<GPUBindGroupLayout?>: a null entry is an empty group.
          .map((layout, index) => (layout == null
            ? null
            : assertLiveResource(layout, path, `descriptor.bindGroupLayouts[${index}]`)));
        const immediateSize = assertIntegerInRange(
          layoutDescriptor.immediateSize ?? 0,
          path,
          'descriptor.immediateSize',
          { min: 0, max: UINT32_MAX },
        );
        const native = backend.deviceCreatePipelineLayout(this, layouts, layoutDescriptor.label || undefined, immediateSize);
        if (native === null) {
          return null;
        }
        const pl = new DoeGPUPipelineLayout(native, this);
        pl.label = layoutDescriptor.label ?? '';
        return pl;
      }, () => markInvalid(new DoeGPUPipelineLayout(null, this), labelOf(descriptor)));
    }

    // Descriptor failures are validation errors; an allocation failure the
    // provider reports as out-of-memory reaches the scopes from the provider.
    createTexture(descriptor) {
      const path = 'GPUDevice.createTexture';
      return createReportingErrors(this, path, () => {
        const textureDescriptor = assertObject(descriptor, path, 'descriptor');
        const size = assertTextureSize(textureDescriptor.size, path);
        const usage = assertIntegerInRange(textureDescriptor.usage, path, 'descriptor.usage', { min: 1 });
        const normalizedDescriptor = normalizeTextureDescriptor(textureDescriptor, size, usage, this.features, path);
        return this._createTextureFromNormalizedDescriptor(descriptor, normalizedDescriptor, size, usage);
      }, () => markInvalid(new DoeGPUTexture(null, this, invalidTextureMeta(descriptor)), labelOf(descriptor)));
    }

    _createTextureFromNormalizedDescriptor(textureDescriptor, normalizedDescriptor, size, usage) {
      const native = backend.deviceCreateTexture(this, normalizedDescriptor, size, usage);
      if (native === null) {
        return null;
      }
      const texture = new DoeGPUTexture(native, this, {
        width: size.width,
        height: size.height,
        depthOrArrayLayers: size.depthOrArrayLayers,
        mipLevelCount: normalizedDescriptor.mipLevelCount ?? 1,
        sampleCount: normalizedDescriptor.sampleCount ?? 1,
        dimension: normalizedDescriptor.dimension,
        format: normalizedDescriptor.format ?? 'rgba8unorm',
        usage,
        textureBindingViewDimension: textureDescriptor.textureBindingViewDimension ?? undefined,
        viewFormats: Array.isArray(textureDescriptor.viewFormats) ? textureDescriptor.viewFormats : [],
      });
      texture.label = normalizedDescriptor.label ?? '';
      texture._features = this.features;
      return texture;
    }

    createSampler(descriptor = {}) {
      const path = 'GPUDevice.createSampler';
      return createReportingErrors(this, path, () => {
        const normalizedDescriptor = normalizeSamplerDescriptor(descriptor, path);
        const native = backend.deviceCreateSampler(this, normalizedDescriptor);
        if (native === null) {
          return null;
        }
        const sampler = new DoeGPUSampler(native, this);
        sampler.label = normalizedDescriptor?.label ?? '';
        return sampler;
      }, () => markInvalid(new DoeGPUSampler(null, this), labelOf(descriptor)));
    }

    // Validates a render pipeline descriptor and returns how to create it;
    // wrap(native) returns the pipeline, or null for a provider rejection.
    _renderPipelineRequest(descriptor, path) {
      const renderDescriptor = assertObject(descriptor, path, 'descriptor');
      const vertex = assertObject(renderDescriptor.vertex, path, 'descriptor.vertex');
      const vertexModule = assertLiveResource(vertex.module, path, 'GPUShaderModule');
      const fragmentStage = normalizeFragmentStage(renderDescriptor, this.features, path);
      const vertexBuffers = vertex.buffers === undefined
        ? []
        : normalizeVertexBufferLayouts(vertex.buffers, path);
      const layout = renderDescriptor.layout === 'auto' || renderDescriptor.layout === undefined
        ? null
        : renderDescriptor.layout;
      if (layout !== null) {
        assertLiveResource(layout, path, 'GPUPipelineLayout');
      }
      const pipelineDescriptor = {
        layout: layout?._native ?? null,
        vertexModule,
        vertexEntryPoint: inferShaderEntryPoint(vertex.module, vertex.entryPoint, 'vertex', path, 'descriptor.vertex.entryPoint'),
        vertexBuffers,
        vertexConstants: vertex.constants ?? null,
        ...fragmentStage,
        primitive: normalizePrimitiveState(renderDescriptor.primitive ?? null, path),
        depthStencil: normalizeDepthStencilState(renderDescriptor.depthStencil ?? null, this.features, path),
        multisample: renderDescriptor.multisample ?? null,
      };
      return {
        create: () => backend.deviceCreateRenderPipeline(this, pipelineDescriptor),
        createAsync: typeof backend.deviceCreateRenderPipelineAsync === 'function'
          ? () => backend.deviceCreateRenderPipelineAsync(this, pipelineDescriptor)
          : null,
        wrap: (native) => {
          if (native === null) {
            return null;
          }
          const autoLayoutEntriesByGroup = layout
            ? null
            : renderAutoLayoutEntriesByGroup({
              layout: renderDescriptor.layout,
              vertexModule: vertex.module,
              fragmentModule: renderDescriptor.fragment?.module ?? null,
            }, path);
          const rp = new DoeGPURenderPipeline(native, this, layout, autoLayoutEntriesByGroup);
          rp.label = renderDescriptor.label ?? '';
          return rp;
        },
      };
    }

    createRenderPipeline(descriptor) {
      const path = 'GPUDevice.createRenderPipeline';
      return createReportingErrors(this, path, () => {
        const request = this._renderPipelineRequest(descriptor, path);
        return request.wrap(shouldRouteDeviceError(this)
          ? request.create()
          : createWithProviderMessage(this, path, request.create));
      }, () => markInvalid(new DoeGPURenderPipeline(null, this, null, null), labelOf(descriptor)), wrapPipelineError);
    }

    // Rejects with GPUPipelineError for every failure; no error reaches a scope.
    async createRenderPipelineAsync(descriptor) {
      const path = 'GPUDevice.createRenderPipelineAsync';
      if (isDeviceLost(this)) {
        return markInvalid(new DoeGPURenderPipeline(null, this, null, null), labelOf(descriptor));
      }
      try {
        const request = this._renderPipelineRequest(descriptor, path);
        const native = request.createAsync
          ? await request.createAsync()
          : createWithProviderMessage(this, path, request.create);
        const pipeline = request.wrap(native);
        if (pipeline === null) {
          throw providerRejection(path);
        }
        return pipeline;
      } catch (error) {
        throw wrapPipelineError(error);
      }
    }

    createRenderBundleEncoder(descriptor) {
      const path = 'GPUDevice.createRenderBundleEncoder';
      if (!encoderClasses?.DoeGPURenderBundleEncoder) {
        failUnsupported(path, 'render bundle encoder surface unavailable on this package build');
      }
      return createReportingErrors(this, path, () => {
        const bundleDescriptor = assertObject(descriptor, path, 'descriptor');
        assertLiveResource(this, path, 'GPUDevice');
        const colorFormats = assertArray(bundleDescriptor.colorFormats ?? [], path, 'descriptor.colorFormats');
        // Feature-gated formats throw TypeError (content timeline).
        colorFormats.forEach((format, index) => {
          if (format != null) normalizeTextureFormat(format, path, `descriptor.colorFormats[${index}]`, this.features);
        });
        if (bundleDescriptor.depthStencilFormat != null) {
          normalizeTextureFormat(bundleDescriptor.depthStencilFormat, path, 'descriptor.depthStencilFormat', this.features);
        }
        if (!colorFormats.some((format) => format != null) && bundleDescriptor.depthStencilFormat == null) {
          failValidation(path, 'descriptor needs a non-null color format or a depthStencilFormat');
        }
        const rbe = backend.deviceCreateRenderBundleEncoder(this, bundleDescriptor, encoderClasses);
        if (rbe === null) {
          return null;
        }
        rbe.label = bundleDescriptor.label ?? '';
        return rbe;
      }, () => {
        const rbe = markInvalid(new encoderClasses.DoeGPURenderBundleEncoder(null, this), labelOf(descriptor));
        rbe._invalidMessage = `${path}: the render bundle encoder is invalid because its creation failed validation`;
        return rbe;
      });
    }

    createQuerySet(descriptor) {
      const path = 'GPUDevice.createQuerySet';
      return createReportingErrors(this, path, () => {
        assertLiveResource(this, path, 'GPUDevice');
        const queryDescriptor = normalizeQuerySetDescriptor(descriptor, path);
        // A timestamp set without timestamp-query is a content-timeline TypeError.
        if (queryDescriptor.type === 'timestamp' && !this.features.has('timestamp-query')) {
          throw new TypeError(`${path}: descriptor.type "timestamp" requires the timestamp-query feature`);
        }
        // An empty query set is valid; only a count past 4096 is an error.
        assertIntegerInRange(queryDescriptor.count, path, 'descriptor.count', { min: 0, max: UINT32_MAX });
        if (queryDescriptor.count > MAX_QUERY_SET_COUNT) {
          failValidation(path, `descriptor.count ${queryDescriptor.count} exceeds ${MAX_QUERY_SET_COUNT}`);
        }
        const native = backend.deviceCreateQuerySet(this, queryDescriptor);
        if (native === null) {
          return null;
        }
        const qs = new DoeGPUQuerySet(native, queryDescriptor.type, queryDescriptor.count, this);
        qs.label = queryDescriptor.label ?? '';
        return qs;
      }, () => markInvalid(
        new DoeGPUQuerySet(null, descriptor?.type, Number.isInteger(descriptor?.count) ? descriptor.count : 0, this),
        labelOf(descriptor),
      ));
    }

    createCommandEncoder(descriptor) {
      if (descriptor !== undefined) {
        assertObject(descriptor, 'GPUDevice.createCommandEncoder', 'descriptor');
      }
      if (!isDeviceLost(this)) {
        assertLiveResource(this, 'GPUDevice.createCommandEncoder', 'GPUDevice');
      }
      const encoder = backend.deviceCreateCommandEncoder(this, descriptor, classes);
      encoder.label = descriptor?.label ?? '';
      return encoder;
    }

    importExternalTexture(descriptor) {
      const native = assertLiveResource(this, 'GPUDevice.importExternalTexture', 'GPUDevice');
      const textureDescriptor = assertObject(descriptor, 'GPUDevice.importExternalTexture', 'descriptor');
      if (typeof backend.deviceImportExternalTexture !== 'function') {
        failValidation(
          'GPUDevice.importExternalTexture',
          'importExternalTexture is not supported on this package surface',
        );
      }
      if (textureDescriptor.source == null) {
        failValidation('GPUDevice.importExternalTexture', 'descriptor.source is required');
      }
      if (textureDescriptor.colorSpace !== undefined) {
        assertNonEmptyString(
          textureDescriptor.colorSpace,
          'GPUDevice.importExternalTexture',
          'descriptor.colorSpace',
        );
      }
      if (textureDescriptor.label !== undefined && typeof textureDescriptor.label !== 'string') {
        failValidation('GPUDevice.importExternalTexture', 'descriptor.label must be a string');
      }
      return backend.deviceImportExternalTexture(this, native, textureDescriptor, classes);
    }

    destroy() {
      // Errors generated before destroy() are still delivered.
      this._drainProviderErrors();
      resolveDeviceLost(this, 'destroyed', 'GPUDevice.destroy() was called');
      destroyResource(this, (native) => backend.deviceDestroy(native, this));
    }
  }

  // WebGPU has no adapter destroy: a native adapter is released when its JS
  // adapter is collected (devices hold their own native references).
  const adapterReleases = typeof FinalizationRegistry === 'function'
    ? new FinalizationRegistry((native) => {
      try { backend.adapterDestroy(native); } catch { /* the addon or library is gone */ }
    })
    : null;

  class DoeGPUAdapter {
    constructor(native, instance, requestOptions = null) {
      if (native != null) adapterReleases?.register(this, native, this);
      this._native = native;
      this._instance = instance;
      this._requestOptions = requestOptions;
      this._features = null;
      this._limits = null;
      this._info = null;
      this._consumed = false;
      this.label = '';
      initResource(this, 'GPUAdapter');
    }

    get features() {
      if (this._features === null) {
        this._features = backend.adapterFeatures(assertLiveResource(this, 'GPUAdapter.features', 'GPUAdapter'));
      }
      return this._features;
    }

    get limits() {
      if (this._limits === null) {
        this._limits = backend.adapterLimits(assertLiveResource(this, 'GPUAdapter.limits', 'GPUAdapter'));
      }
      return this._limits;
    }

    get info() {
      if (this._info !== null) {
        return this._info;
      }
      if (typeof backend.adapterGetInfo === 'function') {
        this._info = backend.adapterGetInfo(this, this._native);
        return this._info;
      }
      this._info = Object.freeze({
        vendor: '',
        architecture: '',
        device: '',
        description: '',
        subgroupMinSize: 0,
        subgroupMaxSize: 0,
      });
      return this._info;
    }

    async requestDevice(descriptor) {
      if (this._consumed) {
        throw operationError('GPUAdapter.requestDevice: adapter has already produced a device');
      }
      assertLiveResource(this, 'GPUAdapter.requestDevice', 'GPUAdapter');
      const normalized = normalizeRequestDeviceDescriptor(descriptor, 'GPUAdapter.requestDevice');
      const resolvedFeatures = resolveDeviceFeatures(this, normalized);
      const resolvedLimits = resolveDeviceLimits(this, normalized);
      const deviceResult = backend.adapterRequestDevice(this, nativeRequestDeviceDescriptor(normalized), classes);
      this._consumed = true;
      let device;
      try {
        device = await deviceResult;
      } finally {
        // A consumed adapter only answers features, limits and info: cache them
        // and release the native adapter now (its device holds its own native
        // references), so adapters do not outlive their use until collection.
        void this.features;
        void this.limits;
        void this.info;
        const native = this._native;
        this._native = null;
        adapterReleases?.unregister(this);
        if (native != null) backend.adapterDestroy(native);
      }
      device._features = resolvedFeatures;
      device._limits = resolvedLimits;
      return device;
    }

    destroy() {
      adapterReleases?.unregister(this);
      destroyResource(this, (native) => backend.adapterDestroy(native));
    }
  }

  class DoeGPU {
    constructor(instance) {
      this._instance = instance;
    }

    get wgslLanguageFeatures() {
      this._wgslLanguageFeatures ??= backend.wgslLanguageFeatures(this._instance);
      return this._wgslLanguageFeatures;
    }

    getPreferredCanvasFormat() {
      return 'bgra8unorm';
    }

    async requestAdapter(options) {
      const normalized = normalizeRequestAdapterOptions(options, 'GPU.requestAdapter');
      if (
        normalized?.featureLevel !== undefined
        && normalized.featureLevel !== 'core'
        && normalized.featureLevel !== 'compatibility'
      ) {
        return null;
      }
      const adapter = await backend.gpuRequestAdapter(
        this,
        normalized,
        classes,
      );
      // A forceFallbackAdapter request resolves to a fallback adapter or null,
      // never to an ordinary one a provider returned instead.
      if (adapter && normalized?.forceFallbackAdapter === true && adapter.info?.isFallbackAdapter !== true) {
        adapter.destroy?.();
        return null;
      }
      return adapter;
    }
  }

  classes = {
    DoeGPUBuffer,
    DoeGPUQueue,
    DoeGPUTexture,
    DoeGPUTextureView,
    DoeGPUSampler,
    DoeGPURenderPipeline,
    DoeGPUShaderModule,
    DoeGPUComputePipeline,
    DoeGPUBindGroupLayout,
    DoeGPUBindGroup,
    DoeGPUPipelineLayout,
    DoeGPUQuerySet,
    DoeGPUDevice,
    DoeGPUAdapter,
    DoeGPU,
  };
  return classes;
}

export {
  createFullSurfaceClasses,
  shouldRouteDeviceError,
  isDeviceLost,
  deliverDeviceError,
  collectDeviceErrors,
  addDeviceEventListener,
  removeDeviceEventListener,
  dispatchDeviceEvent,
  operationError,
  createDeviceLostState,
  resolveDeviceLost,
  GPUError,
  GPUValidationError,
  GPUOutOfMemoryError,
  GPUInternalError,
  GPUPipelineError,
  GPUDeviceLostInfo,
  GPUUncapturedErrorEvent,
};
