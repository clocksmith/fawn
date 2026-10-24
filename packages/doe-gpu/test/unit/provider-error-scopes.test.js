import assert from 'node:assert/strict';
import { captureProviderErrorScopes, NATIVE_ERROR_FILTERS } from '../../src/vendor/webgpu/shared/provider-error-scopes.js';

function scopes() {
  const frames = Object.values(NATIVE_ERROR_FILTERS).map(filter => ({ filter, error: null }));
  const outer = [...frames];
  return {
    frames,
    outer,
    push(filter) { frames.push({ filter, error: null }); },
    pop() { assert(frames.length > outer.length); return frames.pop().error; },
    report(error) {
      const frame = frames.findLast(candidate => candidate.filter === NATIVE_ERROR_FILTERS[error.type]);
      assert(frame);
      frame.error ??= error;
    },
    unchanged() {
      assert.deepEqual(frames, outer);
      assert(outer.every(frame => frame.error === null));
    },
  };
}

// Internal errors must be captured privately too. Otherwise they leak into the
// user's scope and null creation gets a second, invented validation error.
for (const type of Object.keys(NATIVE_ERROR_FILTERS)) {
  const state = scopes();
  const error = { type, message: `first native ${type}` };
  const captured = captureProviderErrorScopes(() => {
    state.report(error);
    return null;
  }, state.push, state.pop);
  assert.deepEqual(captured, { value: null, filter: type, error });
  assert.equal(captured.error, error);
  state.unchanged();
}
{
  const state = scopes();
  const error = { type: 'internal', message: 'native pipeline failed' };
  const captured = captureProviderErrorScopes(() => {
    state.report(error);
    throw new Error('wrapper rejected the missing native handle');
  }, state.push, state.pop);
  assert.equal(captured.error, error);
  assert.equal(captured.filter, 'internal');
  assert.equal(captured.value, null);
  state.unchanged();
}
{
  const state = scopes();
  const failure = new TypeError('host descriptor access failed');
  assert.throws(() => captureProviderErrorScopes(() => { throw failure; }, state.push, state.pop),
    error => error === failure);
  state.unchanged();
  const resource = {};
  assert.deepEqual(captureProviderErrorScopes(() => resource, state.push, state.pop),
    { value: resource, filter: null, error: null });
  state.unchanged();
}
{
  const state = scopes();
  const failure = new Error('scope push failed');
  let pushes = 0;
  assert.throws(() => captureProviderErrorScopes(() => assert.fail('creation must not start'), filter => {
    if (++pushes === 2) throw failure;
    state.push(filter);
  }, state.pop), error => error === failure);
  state.unchanged();
}
console.log('provider-error-scopes.test: ok');
