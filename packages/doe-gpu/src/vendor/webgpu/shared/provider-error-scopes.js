// Native WebGPU filter values and private creation-error capture share one owner.
export const NATIVE_ERROR_FILTERS = Object.freeze({
  validation: 0x00000001,
  'out-of-memory': 0x00000002,
  internal: 0x00000003,
});

const PRIVATE_SCOPE_ORDER = Object.freeze([
  NATIVE_ERROR_FILTERS['out-of-memory'],
  NATIVE_ERROR_FILTERS.internal,
  NATIVE_ERROR_FILTERS.validation,
]);

export function captureProviderErrorScopes(create, pushScope, popScope) {
  let pushed = 0;
  let value = null;
  let hasThrown = false;
  let thrown;
  let error = null;
  try {
    for (const filter of PRIVATE_SCOPE_ORDER) {
      pushScope(filter);
      pushed += 1;
    }
    value = create();
  } catch (cause) {
    hasThrown = true;
    thrown = cause;
  } finally {
    while (pushed > 0) {
      const captured = popScope();
      error ??= captured ?? null;
      pushed -= 1;
    }
  }
  // A provider's typed report precedes the wrapper exception caused by its
  // rejected handle. Unreported host exceptions keep their original identity.
  if (hasThrown && error === null) throw thrown;
  return { value: hasThrown ? null : value, filter: error?.type ?? null, error };
}
