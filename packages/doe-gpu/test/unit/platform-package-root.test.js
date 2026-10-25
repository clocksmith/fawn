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
