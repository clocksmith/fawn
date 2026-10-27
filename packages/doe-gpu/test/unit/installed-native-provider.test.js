// Actual installed file hashes, source identity and containment govern C admission.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, realpathSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve } from 'node:path';

import { assertInstalledNativeProvider } from '../lib/installed-native-provider.js';

const root = realpathSync(mkdtempSync(resolve(tmpdir(), 'doe-installed-identity-')));
const platform = resolve(root, 'node_modules/doe-gpu-platform');
const directory = resolve(platform, 'bin');
mkdirSync(directory, { recursive: true });
const library = resolve(directory, 'libwebgpu_doe_c.dylib');
const compiler = resolve(directory, 'libdoe_compiler.dylib');
const metadata = resolve(directory, 'doe-c-manifest.json');
const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');
const manifest = {
  source_commit: 'a'.repeat(40), source_tree: 'b'.repeat(40),
  source_inputs: { 'runtime/c/native/src/instance.c': 'c'.repeat(64) },
  artifacts: { 'lib/libwebgpu_doe_c.dylib': hash('C provider'),
    'lib/libdoe_compiler.dylib': hash('C compiler') },
};
const provider = {
  loaded: true, doeNative: true, libraryFlavor: 'doe-c',
  doeLibraryPath: library, buildMetadataPath: metadata, buildMetadataSource: 'c-package',
  buildIdentity: { sourceCommit: manifest.source_commit, sourceTree: manifest.source_tree,
    artifactSha256: manifest.artifacts['lib/libwebgpu_doe_c.dylib'] },
};
function reset(next = manifest) {
  writeFileSync(library, 'C provider');
  writeFileSync(compiler, 'C compiler');
  writeFileSync(metadata, JSON.stringify(next));
}
try {
  reset();
  const accepted = assertInstalledNativeProvider(provider, platform);
  assert.equal(accepted.source, 'c-package');
  assert.equal(accepted.librarySha256, provider.buildIdentity.artifactSha256);
  assert.equal(accepted.sourceCommit, manifest.source_commit);
  assert.throws(() => assertInstalledNativeProvider({ ...provider, buildMetadataSource: 'prebuild' }, platform), /C package manifest/);
  assert.throws(() => assertInstalledNativeProvider({ ...provider, libraryFlavor: 'doe-dropin' }, platform), /C package manifest/);
  assert.throws(() => assertInstalledNativeProvider({ ...provider, buildIdentity: {} }, platform), /source identity/);
  assert.throws(() => assertInstalledNativeProvider({ ...provider, loaded: false }, platform), /not a loaded/);
  assert.throws(() => assertInstalledNativeProvider({ ...provider,
    buildIdentity: { ...provider.buildIdentity, artifactSha256: '0'.repeat(64) } }, platform), /library hash/);
  writeFileSync(library, 'wrong provider');
  assert.throws(() => assertInstalledNativeProvider(provider, platform), /library hash/);
  reset();
  writeFileSync(compiler, 'wrong compiler');
  assert.throws(() => assertInstalledNativeProvider(provider, platform), /dependency hash/);
  for (const source_inputs of [{}, [], { '../outside': 'c'.repeat(64) }, { 'runtime/c/file.c': 'bad' }]) {
    reset({ ...manifest, source_inputs });
    assert.throws(() => assertInstalledNativeProvider(provider, platform), /source input/);
  }
  reset({ ...manifest, artifacts: { 'lib/libwebgpu_doe_c.dylib': hash('C provider') } });
  assert.throws(() => assertInstalledNativeProvider(provider, platform), /compiler dependency/);
  reset();
  const outside = resolve(root, 'outside/libwebgpu_doe_c.dylib');
  mkdirSync(resolve(outside, '..'), { recursive: true });
  writeFileSync(outside, 'C provider');
  assert.throws(() => assertInstalledNativeProvider({ ...provider, doeLibraryPath: outside }, platform), /escaped/);
  rmSync(library);
  symlinkSync(outside, library);
  assert.throws(() => assertInstalledNativeProvider(provider, platform), /escaped/);
  rmSync(library);
  reset();
  const outsideManifest = resolve(root, 'outside/doe-c-manifest.json');
  writeFileSync(outsideManifest, JSON.stringify(manifest));
  assert.throws(() => assertInstalledNativeProvider({ ...provider, buildMetadataPath: outsideManifest }, platform), /manifest escaped/);
  const zig = resolve(directory, 'libwebgpu_doe.dylib');
  writeFileSync(zig, 'legacy Zig provider');
  assert.equal(assertInstalledNativeProvider({ ...provider, doeLibraryPath: zig,
    libraryFlavor: 'doe-dropin', buildMetadataSource: 'prebuild' }, platform).source, 'prebuild');
  console.log('installed native C identity, dependency and escape controls passed');
} finally {
  rmSync(root, { recursive: true, force: true });
}
