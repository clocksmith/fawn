// Callback ownership is observed through the public wrapper, with no GPU needed.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { setImmediate as nextTurn } from 'node:timers/promises';

const source = fs.readFileSync(new URL('../browser/fresh_launch/observer.js', import.meta.url), 'utf8');
async function fixture(autoCompletion) {
  let calls = 0;
  class GPU { requestAdapter() { return Promise.resolve(null); } }
  class GPUAdapter { requestDevice() { return Promise.resolve(null); } }
  class GPUQueue {
    submit() {}
    onSubmittedWorkDone() { calls += 1; return Promise.resolve(); }
  }
  class GPUCanvasContext { getCurrentTexture() { return {}; } }
  const context = vm.createContext({GPU, GPUAdapter, GPUQueue, GPUCanvasContext, performance,
    queueMicrotask, requestAnimationFrame: () => 0, addEventListener: () => {}, __benchAutoCompletion: autoCompletion});
  vm.runInContext(source, context);
  return {context, queue: new GPUQueue(), canvas: new GPUCanvasContext(), calls: () => calls};
}
{
  const f = await fixture(true);
  f.canvas.getCurrentTexture(); f.queue.submit([]); await f.queue.onSubmittedWorkDone(); await nextTurn();
  assert.equal(f.calls(), 1, 'a page-owned completion prevents the extra injected completion');
  assert.equal(f.context.__bench.pageCompletionCalls, 1);
  assert.equal(f.context.__bench.injectedCompletionCalls, 0);
  assert.equal(f.context.__bench.completions[0][4], 0);
}
{
  const f = await fixture(true);
  f.canvas.getCurrentTexture(); f.queue.submit([]); await nextTurn();
  assert.equal(f.calls(), 1, 'observer injects a completion when the page does not');
  assert.equal(f.context.__bench.pageCompletionCalls, 0);
  assert.equal(f.context.__bench.injectedCompletionCalls, 1);
  assert.equal(f.context.__bench.completions[0][4], 1);
}
{
  const f = await fixture(false);
  f.canvas.getCurrentTexture(); f.queue.submit([]); await nextTurn();
  assert.equal(f.calls(), 0, 'disabling injection does not fabricate a completion');
  await f.queue.onSubmittedWorkDone(); await nextTurn();
  assert.equal(f.context.__bench.pageCompletionCalls, 1);
  assert.equal(f.context.__bench.injectedCompletionCalls, 0);
}
console.log('Browser observer: page-owned callbacks, injected callbacks, and disabled injection verified.');
