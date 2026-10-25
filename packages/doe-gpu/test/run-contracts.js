#!/usr/bin/env node

const contracts = [
  './unit/browser-compiler.test.js',
  './unit/compute-program.test.js',
  './unit/live-simulation.test.js',
  './unit/live-simulation-terminal.test.js',
  './unit/node-process-requests.test.js',
  './unit/browser-runtime-identity.test.js',
  './unit/node-webgpu-provider-v1.test.js',
  './unit/node-webgpu-governed-execution.test.js',
  './unit/node-webgpu-loader.test.js',
  './unit/build-metadata-permission.test.js',
  './unit/c-build-metadata.test.js',
  './unit/platform-package-root.test.js',
  './unit/node-webgpu-process.test.js',
  './unit/node-webgpu-process-cli.test.js',
  './unit/root-export-parity.test.js',
  './unit/program-bundle-runner.test.js',
  './unit/full-surface-lifecycle.test.js',
  './unit/provider-error-scopes.test.js',
  './unit/compute-buffer-identity.test.js',
  './unit/one-shot-resources.test.js',
  './unit/capability-publication.test.js',
  './unit/provider-diagnostics.test.js',
  './unit/provider-path.test.js',
  './unit/runtime-selection.test.js',
  './unit/stage-platform-freshness.test.js',
  './unit/plan-contracts.test.js',
  './unit/plan-refactor-receipt.test.js',
  './unit/transparent-webgpu-observer.test.js',
  './unit/native-device-test-helper.test.js',
  './unit/native-release-candidate-bundle.test.js',
  './unit/build-addon.test.js',
  './unit/addon-enum-values.test.js',
  './unit/bun-enum-values.test.js',
  './unit/addon-source-size.test.js',
  './unit/addon-entry-points.test.js',
  './unit/js-style.test.js',
];

for (const contract of contracts) {
  await import(new URL(contract, import.meta.url));
}
