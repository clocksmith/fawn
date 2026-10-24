// The C runtime's provider metadata is its package-stage manifest, accepted only
// when it records the selected library's exact digest; it never reads drop-in
// build/proof metadata, and the drop-in never reads it.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';
import {
  loadDoeBuildMetadata,
  UNKNOWN_DOE_BUILD_METADATA,
} from '../../src/vendor/webgpu/build-metadata.js';
import { libraryBasenamesForPlatform } from '../../src/vendor/webgpu/platform-package.js';

const dropin = libraryBasenamesForPlatform()[0];
const cName = dropin.replace('webgpu_doe', 'webgpu_doe_c');
const root = mkdtempSync(resolve(tmpdir(), 'doe-c-build-metadata-'));
const originalMetadata = process.env.DOE_BUILD_METADATA;
const zigSidecar = { schemaVersion: 1, artifact: 'libwebgpu_doe', leanVerifiedBuild: true, proofArtifactSha256: 'zig-proof' };

function write(path, contents) {
  mkdirSync(resolve(path, '..'), { recursive: true });
  writeFileSync(path, contents);
}
function manifest(digest, extra = {}) {
  return JSON.stringify({ artifacts: { [`lib/${cName}`]: digest }, source_commit: 'abc123',
    source_tree: 'tree456', qualification: 'partial Linux developer package', ...extra });
}

try {
  // runtime/c stage: <stage>/lib/<library> with <stage>/MANIFEST.json.
  const stageLibrary = resolve(root, 'stage/lib', cName);
  write(stageLibrary, 'C library bytes');
  const digest = createHash('sha256').update('C library bytes').digest('hex');
  write(resolve(root, 'stage/MANIFEST.json'), manifest(digest));
  // A drop-in sidecar beside the C library and DOE_BUILD_METADATA must both be ignored.
  write(resolve(root, 'stage/lib/doe-build-metadata.json'), JSON.stringify(zigSidecar));
  write(resolve(root, 'stage/share/doe-build-metadata.json'), JSON.stringify(zigSidecar));
  process.env.DOE_BUILD_METADATA = resolve(root, 'stage/share/doe-build-metadata.json');
  assert.deepEqual(loadDoeBuildMetadata({ libraryPath: stageLibrary }), {
    source: 'c-stage',
    path: resolve(root, 'stage/MANIFEST.json'),
    leanVerifiedBuild: null,
    proofArtifactSha256: null,
    identity: { sourceCommit: 'abc123', sourceTree: 'tree456', artifactSha256: digest,
      qualification: 'partial Linux developer package' },
  }, 'a stage manifest that records the library digest describes it');

  // Platform package: bin/<library> with bin/doe-c-manifest.json (the stage manifest).
  const packageLibrary = resolve(root, 'package/bin', cName);
  write(packageLibrary, 'C library bytes');
  write(resolve(root, 'package/bin/doe-c-manifest.json'), manifest(digest, { source_tree: 7 }));
  const packaged = loadDoeBuildMetadata({ libraryPath: packageLibrary });
  assert.equal(packaged.source, 'c-package');
  assert.equal(packaged.identity.sourceTree, null, 'a non-string field is unknown, not coerced');

  // A manifest for other bytes, or without the artifact, describes nothing.
  write(resolve(root, 'package/bin/doe-c-manifest.json'), manifest('0'.repeat(64)));
  assert.deepEqual(loadDoeBuildMetadata({ libraryPath: packageLibrary }), UNKNOWN_DOE_BUILD_METADATA);
  write(resolve(root, 'package/bin/doe-c-manifest.json'), JSON.stringify({ artifacts: {} }));
  assert.deepEqual(loadDoeBuildMetadata({ libraryPath: packageLibrary }), UNKNOWN_DOE_BUILD_METADATA);
  assert.deepEqual(loadDoeBuildMetadata({ libraryPath: resolve(root, 'missing/lib', cName) }),
    UNKNOWN_DOE_BUILD_METADATA, 'no manifest means unknown');

  // The drop-in never reads a C manifest beside it.
  delete process.env.DOE_BUILD_METADATA;
  const dropinLibrary = resolve(root, 'stage/lib', dropin);
  write(dropinLibrary, 'drop-in bytes');
  assert.equal(loadDoeBuildMetadata({ libraryPath: dropinLibrary }).source, 'adjacent');
  assert.equal(loadDoeBuildMetadata({ libraryPath: dropinLibrary }).identity, undefined);
} finally {
  if (originalMetadata === undefined) delete process.env.DOE_BUILD_METADATA;
  else process.env.DOE_BUILD_METADATA = originalMetadata;
  rmSync(root, { recursive: true, force: true });
}
console.log('C runtime manifest metadata attribution: ok');
