import assert from 'node:assert/strict';
import { isInstalledPackageRoot } from '../../src/vendor/webgpu/platform-package.js';

assert.equal(
  isInstalledPackageRoot('/opt/app/node_modules/doe-gpu'),
  true,
  'ordinary npm installations must prefer their platform package',
);
assert.equal(
  isInstalledPackageRoot('/opt/app/node_modules/.pnpm/doe-gpu@0.4.7/node_modules/doe-gpu'),
  true,
  'pnpm installations must prefer their platform package',
);
assert.equal(
  isInstalledPackageRoot('C:\\app\\node_modules\\doe-gpu'),
  true,
  'Windows npm installations must prefer their platform package',
);
assert.equal(
  isInstalledPackageRoot('/workspace/packages/doe-gpu'),
  false,
  'workspace source must retain development-artifact precedence',
);

console.log('platform package root contracts: ok');

// C-default discovery is explicit: an old sibling is not an automatic fallback.
const { cLibraryBasenamesForPlatform, cWorkspaceLibraryCandidates,
  resolvePlatformPackageLibraryPath } = await import('../../src/vendor/webgpu/platform-package.js');
const { mkdtempSync, mkdirSync, writeFileSync, rmSync } = await import('node:fs');
const { tmpdir } = await import('node:os');
const { join } = await import('node:path');
const directory = mkdtempSync(join(tmpdir(), 'doe-c-default-'));
const oldStage = process.env.DOE_C_STAGE;
try {
  process.env.DOE_C_STAGE = directory;
  assert.deepEqual(cLibraryBasenamesForPlatform('linux'), ['libwebgpu_doe_c.so']);
  assert.equal(cWorkspaceLibraryCandidates(directory, 'win32')[0], join(directory, 'bin/webgpu_doe_c.dll'));
  mkdirSync(join(directory, 'bin'));
  writeFileSync(join(directory, 'package.json'), '{}');
  writeFileSync(join(directory, 'bin/libwebgpu_doe.so'), 'legacy');
  const options = { platform: 'linux', arch: 'x64',
    requireFn: { resolve: () => join(directory, 'package.json') } };
  assert.equal(resolvePlatformPackageLibraryPath(options), null);
  writeFileSync(join(directory, 'bin/libwebgpu_doe_c.so'), 'C runtime');
  assert.equal(resolvePlatformPackageLibraryPath(options), join(directory, 'bin/libwebgpu_doe_c.so'));
} finally {
  if (oldStage === undefined) delete process.env.DOE_C_STAGE; else process.env.DOE_C_STAGE = oldStage;
  rmSync(directory, { recursive: true, force: true });
}

// Platform package names resolve in the documented order: the scoped Windows
// package first, the unscoped name second, and an explicit null when neither
// is installed. Other tuples keep their single unscoped name.
const { platformPackageNames, resolveInstalledPlatformPackageRoot } =
  await import('../../src/vendor/webgpu/platform-package.js');
assert.deepEqual(platformPackageNames('win32', 'x64'),
  ['@clocksmith/doe-gpu-win32-x64', 'doe-gpu-win32-x64']);
assert.deepEqual(platformPackageNames('darwin', 'arm64'), ['doe-gpu-darwin-arm64']);
assert.deepEqual(platformPackageNames('linux', 'x64'), ['doe-gpu-linux-x64']);
assert.deepEqual(platformPackageNames('win32', 'arm64'), []);
function installedOnly(installed) {
  const requested = [];
  return {
    requested,
    resolve(specifier) {
      requested.push(specifier);
      const name = specifier.replace(/\/package\.json$/u, '');
      if (!installed[name]) throw new Error(`Cannot find module '${specifier}'`);
      return join(installed[name], 'package.json');
    },
  };
}
const emptyWorkspace = mkdtempSync(join(tmpdir(), 'doe-no-workspace-'));
try {
  const windows = { platform: 'win32', arch: 'x64', workspaceRoot: emptyWorkspace };
  const both = installedOnly({
    '@clocksmith/doe-gpu-win32-x64': '/scoped', 'doe-gpu-win32-x64': '/unscoped',
  });
  assert.equal(resolveInstalledPlatformPackageRoot({ ...windows, requireFn: both }), '/scoped');
  const unscoped = installedOnly({ 'doe-gpu-win32-x64': '/unscoped' });
  assert.equal(resolveInstalledPlatformPackageRoot({ ...windows, requireFn: unscoped }), '/unscoped');
  assert.deepEqual(unscoped.requested,
    ['@clocksmith/doe-gpu-win32-x64/package.json', 'doe-gpu-win32-x64/package.json']);
  assert.equal(resolveInstalledPlatformPackageRoot({ ...windows, requireFn: installedOnly({}) }), null);
  const linux = installedOnly({ 'doe-gpu-linux-x64': '/linux' });
  assert.equal(resolveInstalledPlatformPackageRoot({
    platform: 'linux', arch: 'x64', workspaceRoot: emptyWorkspace, requireFn: linux,
  }), '/linux');
  // The workspace fallback is the unscoped tuple directory.
  mkdirSync(join(emptyWorkspace, 'packages', 'doe-gpu-win32-x64'), { recursive: true });
  writeFileSync(join(emptyWorkspace, 'packages', 'doe-gpu-win32-x64', 'package.json'), '{}');
  assert.equal(resolveInstalledPlatformPackageRoot({ ...windows, requireFn: installedOnly({}) }),
    join(emptyWorkspace, 'packages', 'doe-gpu-win32-x64'));
} finally {
  rmSync(emptyWorkspace, { recursive: true, force: true });
}
console.log('platform package name order contracts: ok');
