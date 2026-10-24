const MAX_SAFE_U64 = Number.MAX_SAFE_INTEGER;
const UINT32_MAX = 0xFFFF_FFFF;
const RESOURCE_RELEASES = new WeakMap();

function failValidation(path, message) {
  throw new Error(`${path}: ${message}`);
}

// A Doe capability gap rather than a WebGPU validation failure: it always
// throws, even where a validation error would go to an error scope.
function failUnsupported(path, message) {
  const error = new Error(`${path}: ${message}`);
  error.code = 'DOE_UNSUPPORTED';
  throw error;
}

function describeResourceLabel(value, fallback = 'resource') {
  return value?._resourceLabel ?? fallback;
}

function initResource(target, label, owner = null, release = null) {
  target._resourceLabel = label;
  target._resourceOwner = owner;
  target._destroyed = false;
  target._invalid = false;
  if (release) RESOURCE_RELEASES.set(target, release);
  return target;
}

// An invalid object (WebGPU "invalid" state) results from a creation whose
// validation error was reported to an error scope or uncapturederror listener.
// Every later use of it is itself a validation error.
function markInvalid(target, label = '') {
  target._invalid = true;
  target.label = typeof label === 'string' ? label : '';
  return target;
}

function isInvalidResource(value) {
  return value?._invalid === true;
}

function releaseOwnedResource(resource) {
  const release = RESOURCE_RELEASES.get(resource);
  if (release) destroyResource(resource, release);
  else resource.destroy?.();
}

function assertObject(value, path, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    failValidation(path, `${label} must be an object`);
  }
  return value;
}

function assertArray(value, path, label) {
  if (!Array.isArray(value)) {
    failValidation(path, `${label} must be an array`);
  }
  return value;
}

function assertBoolean(value, path, label) {
  if (typeof value !== 'boolean') {
    failValidation(path, `${label} must be a boolean`);
  }
  return value;
}

function assertString(value, path, label) {
  if (typeof value !== 'string') {
    failValidation(path, `${label} must be a string`);
  }
  return value;
}

function assertNonEmptyString(value, path, label) {
  if (assertString(value, path, label).length === 0) {
    failValidation(path, `${label} must not be empty`);
  }
  return value;
}

function assertIntegerInRange(value, path, label, options) {
  const min = options ? (options.min ?? 0) : 0;
  const max = options ? (options.max ?? MAX_SAFE_U64) : MAX_SAFE_U64;
  if (!Number.isInteger(value) || value < min || value > max) {
    failValidation(path, `${label} must be an integer in [${min}, ${max}]`);
  }
  return value;
}

function assertOptionalIntegerInRange(value, path, label, options) {
  if (value === undefined) {
    return value;
  }
  return assertIntegerInRange(value, path, label, options);
}

function validatePositiveInteger(value, label) {
  if (!Number.isInteger(value) || value < 1) {
    throw new Error(`${label} must be a positive integer.`);
  }
}

function assertLiveResource(resource, path, label = null) {
  const native = resource?._native;
  if (native != null && !resource._destroyed && !resource._invalid) {
    const owner = resource._resourceOwner;
    if (!owner || !owner._destroyed) return native;
    failValidation(
      path,
      `${label ?? describeResourceLabel(resource)} cannot be used after ${describeResourceLabel(owner, 'owning resource')} was destroyed`,
    );
  }
  const resourceLabel = label ?? describeResourceLabel(resource);
  if (!resource || typeof resource !== 'object' || !('_native' in resource)) {
    failValidation(path, `${resourceLabel} must be a Doe WebGPU object`);
  }
  if (resource._invalid === true) {
    failValidation(path, `${resourceLabel} is invalid because its creation failed validation`);
  }
  failValidation(path, `${resourceLabel} was destroyed`);
}

function destroyResource(resource, release) {
  if (resource._destroyed || resource._native == null) {
    return;
  }
  release(resource._native);
  resource._native = null;
  resource._destroyed = true;
}

// GPUOrigin3D: a sequence [x, y, z] or a dictionary; omitted members are 0.
function normalizeOrigin3D(origin, path, label) {
  if (origin === undefined) return { x: 0, y: 0, z: 0 };
  if (Array.isArray(origin)) {
    if (origin.length > 3) failValidation(path, `${label} must have at most 3 entries`);
    return { x: origin[0] ?? 0, y: origin[1] ?? 0, z: origin[2] ?? 0 };
  }
  const value = assertObject(origin, path, label);
  return { x: value.x ?? 0, y: value.y ?? 0, z: value.z ?? 0 };
}

// GPUExtent3D: a sequence [width, height, depth] with 1 to 3 entries, or a
// dictionary with a required width; height and depthOrArrayLayers default to 1.
function normalizeExtent3D(extent, path, label) {
  if (Array.isArray(extent)) {
    if (extent.length < 1 || extent.length > 3) failValidation(path, `${label} must have 1 to 3 entries`);
    return { width: extent[0], height: extent[1] ?? 1, depthOrArrayLayers: extent[2] ?? 1 };
  }
  const value = assertObject(extent, path, label);
  if (value.width === undefined) failValidation(path, `${label}.width is required`);
  return { width: value.width, height: value.height ?? 1, depthOrArrayLayers: value.depthOrArrayLayers ?? 1 };
}

export {
  MAX_SAFE_U64,
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
  assertString,
  assertNonEmptyString,
  assertIntegerInRange,
  assertOptionalIntegerInRange,
  validatePositiveInteger,
  assertLiveResource,
  destroyResource,
  releaseOwnedResource,
  normalizeOrigin3D,
  normalizeExtent3D,
};
