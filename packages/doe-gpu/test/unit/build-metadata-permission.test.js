import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import {
  loadDoeBuildMetadata,
  UNKNOWN_DOE_BUILD_METADATA,
} from '../../src/vendor/webgpu/build-metadata.js';
import { libraryBasenamesForPlatform } from '../../src/vendor/webgpu/platform-package.js';

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = resolve(here, '../..');
const modulePath = resolve(packageRoot, 'src/vendor/webgpu/build-metadata.js');
const platformModulePath = resolve(packageRoot, 'src/vendor/webgpu/platform-package.js');
const fixtureRoot = resolve(here, '../fixtures/build-metadata-permission');
const sidecarPath = resolve(fixtureRoot, 'share/doe-build-metadata.json');
const libraryPath = resolve(fixtureRoot, 'lib', libraryBasenamesForPlatform()[0]);
const packageJsonPath = resolve(packageRoot, 'package.json');

const source = `
  import { loadDoeBuildMetadata } from ${JSON.stringify(pathToFileURL(modulePath).href)};
  const result = loadDoeBuildMetadata({ libraryPath: ${JSON.stringify(libraryPath)} });
  process.stdout.write(JSON.stringify(result));
`;
const child = spawnSync(process.execPath, [
  '--permission',
  `--allow-fs-read=${modulePath}`,
  `--allow-fs-read=${platformModulePath}`,
  `--allow-fs-read=${packageJsonPath}`,
  `--allow-fs-read=${sidecarPath}`,
  '--input-type=module',
  '--eval',
  source,
], { encoding: 'utf8', env: { ...process.env, DOE_BUILD_METADATA: '' } });

assert.equal(child.status, 0, child.stderr);
assert.equal(child.signal, null);
assert.deepEqual(JSON.parse(child.stdout), {
  source: 'workspace',
  path: sidecarPath,
  leanVerifiedBuild: false,
  proofArtifactSha256: null,
});

const root = mkdtempSync(resolve(tmpdir(), 'doe-build-metadata-'));
const originalMetadata = process.env.DOE_BUILD_METADATA;
delete process.env.DOE_BUILD_METADATA;
try {
  const prebuild = resolve(root, 'package/prebuilds', `${process.platform}-${process.arch}`);
  const selectedPackageLibrary = resolve(prebuild, libraryBasenamesForPlatform()[0]);
  const externalLibrary = resolve(root, 'external/lib', libraryBasenamesForPlatform()[0]);
  const prebuildMetadata = resolve(prebuild, 'metadata.json');
  const metadata = {
    schemaVersion: 1,
    artifact: 'libwebgpu_doe',
    leanVerifiedBuild: true,
    proofArtifactSha256: 'proof-for-package-library',
  };
  mkdirSync(prebuild, { recursive: true });
  writeFileSync(prebuildMetadata, JSON.stringify({ schemaVersion: 1, doeBuild: metadata }));
  const options = { packageRoot: resolve(root, 'package') };
  assert.deepEqual(loadDoeBuildMetadata({ ...options, libraryPath: selectedPackageLibrary }), {
    source: 'prebuild',
    path: prebuildMetadata,
    leanVerifiedBuild: true,
    proofArtifactSha256: 'proof-for-package-library',
  }, 'a selected package library keeps its adjacent build and proof metadata');
  assert.deepEqual(loadDoeBuildMetadata({ ...options, libraryPath: externalLibrary }),
    UNKNOWN_DOE_BUILD_METADATA, 'external overrides cannot borrow package build or proof metadata');
  assert.deepEqual(loadDoeBuildMetadata(options), UNKNOWN_DOE_BUILD_METADATA,
    'no selected artifact means no attributable build metadata');

  const share = resolve(root, 'external/share');
  const sidecar = resolve(share, 'doe-build-metadata.json');
  mkdirSync(share, { recursive: true });
  writeFileSync(sidecar, JSON.stringify(metadata));
  assert.equal(loadDoeBuildMetadata({ ...options, libraryPath: externalLibrary }).source,
    'workspace', 'a selected drop-in can use its own workspace sidecar');
  process.env.DOE_BUILD_METADATA = sidecar;
  assert.equal(loadDoeBuildMetadata({ libraryPath: externalLibrary }).source,
    'env', 'an explicit sidecar still takes precedence for a selected drop-in');
  const dropinName = libraryBasenamesForPlatform()[0];
  for (const library of [dropinName.replace('webgpu_doe', 'webgpu_doe_c'),
    dropinName.replace('webgpu_doe', 'webgpu_dawn'),
    dropinName.replace('webgpu_doe', 'custom_provider')]) {
    assert.deepEqual(loadDoeBuildMetadata({ ...options,
      libraryPath: resolve(root, 'external/lib', library) }), UNKNOWN_DOE_BUILD_METADATA,
    'C, delegated and unknown artifacts cannot inherit legacy drop-in metadata');
  }
} finally {
  if (originalMetadata === undefined) delete process.env.DOE_BUILD_METADATA;
  else process.env.DOE_BUILD_METADATA = originalMetadata;
  rmSync(root, { recursive: true, force: true });
}

console.log('build metadata attribution and permission contracts: ok');
