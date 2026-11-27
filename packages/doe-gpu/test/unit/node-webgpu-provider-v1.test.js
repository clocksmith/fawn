import assert from 'node:assert/strict';
import {
  NodeWebGPUProviderError,
  openNodeWebGPU,
  probeNodeWebGPU,
} from '../../src/node-webgpu.js';

const fixtureUrl = new URL('../fixtures/provider-v1.js', import.meta.url).href;
const globalNames = [
  'navigator',
  'GPUBufferUsage',
  'GPUShaderStage',
  'GPUMapMode',
  'GPUTextureUsage',
];
const initialDescriptors = new Map(
  globalNames.map((name) => [name, Object.getOwnPropertyDescriptor(globalThis, name)]),
);
const initialNavigator = globalThis.navigator;
const initialGpu = initialNavigator && Object.getOwnPropertyDescriptor(initialNavigator, 'gpu');
function assertRestoredGlobals(except = null) {
  for (const name of globalNames) {
    if (name !== except) {
      assert.deepEqual(Object.getOwnPropertyDescriptor(globalThis, name), initialDescriptors.get(name), name);
    }
  }
  if (initialNavigator) assert.deepEqual(Object.getOwnPropertyDescriptor(initialNavigator, 'gpu'), initialGpu);
}
const globals = {
  GPUBufferUsage: 'globals.GPUBufferUsage',
  GPUShaderStage: 'globals.GPUShaderStage',
  GPUMapMode: 'globals.GPUMapMode',
  GPUTextureUsage: 'globals.GPUTextureUsage',
};

const session = await openNodeWebGPU({
  providers: [
    {
      id: 'declared-failure',
      kind: 'module',
      module: fixtureUrl,
      gpu: { kind: 'factory', path: 'failFactory', args: ['only-once'] },
      globals,
    },
    {
      id: 'declared-success',
      kind: 'module',
      module: fixtureUrl,
      gpu: { kind: 'factory', path: 'createFakeGPU', args: ['exact-argument'] },
      globals,
    },
  ],
  adapterOptions: { powerPreference: 'low-power' },
  globals: { mode: 'replace' },
});

assert.equal(session.receipt.ok, true);
assert.equal(session.receipt.selectedProviderId, 'declared-success');
assert.deepEqual(session.receipt.providerOrder, ['declared-failure', 'declared-success']);
assert.deepEqual(session.receipt.providers[1].gpu.args, ['exact-argument']);
assert.equal(session.receipt.attempts[0].code, 'DOE_PROVIDER_FACTORY_FAILED');
assert.equal(session.receipt.attempts[1].ok, true);
assert.ok(session.receipt.globals.installed.includes('navigator.gpu'));
assert.equal(globalThis.navigator.gpu, session.gpu);

const observations = session.module.getProviderObservations();
assert.deepEqual(observations.lastFactoryArgs, ['exact-argument']);
assert.deepEqual(observations.lastAdapterOptions, { powerPreference: 'low-power' });

await session.close();
assert.equal(session.receipt.globals.restored, true);
assertRestoredGlobals();

const openFixture = () => openNodeWebGPU({
  providers: [{
    id: 'fixture', kind: 'module', module: fixtureUrl,
    gpu: { kind: 'factory', path: 'createFakeGPU', args: [] }, globals,
  }],
  adapterOptions: null,
  globals: { mode: 'replace' },
});

for (const olderClosesFirst of [true, false]) {
  const older = await openFixture();
  const newer = await openFixture();
  assert.notEqual(older.gpu, newer.gpu);
  const [first, last] = olderClosesFirst ? [older, newer] : [newer, older];
  await first.close();
  assert.equal(globalThis.navigator.gpu, last.gpu);
  await first.close();
  await last.close();
  assertRestoredGlobals();
}

const owner = await openFixture();
const borrower = await openNodeWebGPU({
  providers: [{ id: 'installed', kind: 'global' }],
  adapterOptions: null,
  globals: { mode: 'install-missing' },
});
await owner.close();
assert.equal(globalThis.navigator.gpu, borrower.gpu);
assert.equal(globalThis.GPUMapMode, owner.module.globals.GPUMapMode);
await borrower.close();
assertRestoredGlobals();

// Force a conflict after navigator.gpu and the first enum have been installed.
const conflict = { occupied: true };
Object.defineProperty(globalThis, 'GPUMapMode', { value: conflict, configurable: true });
try {
  const result = await probeNodeWebGPU({
    providers: [{
      id: 'rollback', kind: 'module', module: fixtureUrl,
      gpu: { kind: 'factory', path: 'createFakeGPU', args: [] }, globals,
    }],
    adapterOptions: null,
    globals: { mode: 'install-missing' },
  });
  assert.equal(result.receipt.attempts[0].code, 'DOE_PROVIDER_GLOBAL_CONFLICT');
  assert.equal(globalThis.GPUMapMode, conflict);
  assertRestoredGlobals('GPUMapMode');
} finally {
  const before = initialDescriptors.get('GPUMapMode');
  if (before) Object.defineProperty(globalThis, 'GPUMapMode', before);
  else delete globalThis.GPUMapMode;
}

const failedProbe = await probeNodeWebGPU({
  providers: [{
    id: 'only-failure',
    kind: 'module',
    module: fixtureUrl,
    gpu: { kind: 'factory', path: 'failFactory', args: [] },
    globals,
  }],
  adapterOptions: null,
  globals: { mode: 'none' },
});
assert.equal(failedProbe.ok, false);
assert.ok(failedProbe.error instanceof NodeWebGPUProviderError);
assert.equal(failedProbe.error.code, 'DOE_PROVIDER_ALL_FAILED');
assert.equal(failedProbe.receipt.attempts[0].code, 'DOE_PROVIDER_FACTORY_FAILED');

await assert.rejects(
  () => openNodeWebGPU({
    providers: [],
    adapterOptions: null,
    globals: { mode: 'none' },
  }),
  (error) => error instanceof NodeWebGPUProviderError
    && error.code === 'DOE_PROVIDER_INVALID_CONFIGURATION',
);

console.log('node-webgpu provider-v1 contracts: ok');
