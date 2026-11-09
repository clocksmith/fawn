// Shared contract-validation primitives for governed Node WebGPU processes.

const SHA256_PATTERN = /^sha256:[a-f0-9]{64}$/;

function assertPlainObject(value, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new TypeError(`${label} must be an object.`);
  }
  return value;
}

function assertKnownKeys(value, allowed, label) {
  for (const key of Object.keys(value)) {
    if (!allowed.has(key)) throw new TypeError(`${label} contains unsupported field "${key}".`);
  }
}

function assertNonEmptyString(value, label) {
  if (typeof value !== 'string' || value.trim().length === 0) {
    throw new TypeError(`${label} must be a non-empty string.`);
  }
  return value;
}

function assertSha256(value, label) {
  if (typeof value !== 'string' || !SHA256_PATTERN.test(value)) {
    throw new TypeError(`${label} must be a lowercase sha256:<64 hex> digest.`);
  }
  return value;
}

function assertStringArray(value, label) {
  if (!Array.isArray(value) || value.some((item) => typeof item !== 'string')) {
    throw new TypeError(`${label} must be an array of strings.`);
  }
  return [...value];
}

export {
  SHA256_PATTERN,
  assertKnownKeys,
  assertNonEmptyString,
  assertPlainObject,
  assertSha256,
  assertStringArray,
};
