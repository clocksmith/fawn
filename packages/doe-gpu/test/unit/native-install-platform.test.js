// Package dependency versions and local installed-runtime tuples agree.
import assert from 'node:assert/strict';
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { nativeInstallPlatform } from '../lib/native-install-platform.js';
import { platformPackageNames, resolvePlatformPackageLibraryPath } from '../../src/vendor/webgpu/platform-package.js';

const wrapper = JSON.parse(readFileSync(new URL('../../package.json', import.meta.url)));
for (const [platform, arch, libraryName] of [
  ['linux', 'x64', 'libwebgpu_doe_c.so'],
  ['darwin', 'arm64', 'libwebgpu_doe_c.dylib'],
  ['win32', 'x64', 'webgpu_doe_c.dll'],
]) {
  const tuple = nativeInstallPlatform(platform, arch);
  assert.equal(tuple.libraryName, libraryName);
  assert.equal(tuple.packageDirectory, `doe-gpu-${platform}-${arch}`);
  // The installed package is the loader's first admitted name for the tuple.
  assert.equal(platformPackageNames(platform, arch)[0], tuple.packageName);
  const manifest = JSON.parse(readFileSync(new URL(`../../../${tuple.packageDirectory}/package.json`, import.meta.url)));
  const directory = new URL(`../../../${tuple.packageDirectory}/`, import.meta.url);
  if (platform === 'darwin') {
    const installedRoot = realpathSync(mkdtempSync(join(tmpdir(), 'doe-darwin-export-')));
    try {
      const bin = join(installedRoot, 'bin');
      mkdirSync(bin);
      copyFileSync(new URL('index.js', directory), join(installedRoot, 'index.mjs'));
      const stageManifest = join(bin, 'doe-c-manifest.json');
      const filename = 'libwebgpu_fixture_c.dylib';
      const abi = 'a'.repeat(64);
      const artifact = {
        native_library: { platform, arch, filename, abi },
        artifacts: { 'include/webgpu.h': abi },
        source_inputs: { 'runtime/vendor/webgpu-headers/webgpu.h': abi },
      };
      writeFileSync(stageManifest, JSON.stringify(artifact));
      writeFileSync(join(bin, filename), 'fixture');
      const { libraryPath, addonPath } = await import(pathToFileURL(join(installedRoot, 'index.mjs')));
      assert.equal(libraryPath, join(installedRoot, 'bin', filename));
      assert.equal(addonPath, join(installedRoot, 'bin', 'doe_napi.node'));
      const options = { platform, arch, requireFn: { resolve: () => join(installedRoot, 'package.json') } };
      assert.equal(resolvePlatformPackageLibraryPath(options), libraryPath);
      artifact.native_library.arch = 'x64';
      writeFileSync(stageManifest, JSON.stringify(artifact));
      assert.equal(resolvePlatformPackageLibraryPath(options), null);
    } finally {
      rmSync(installedRoot, { recursive: true, force: true });
    }
  } else {
    const { libraryPath, addonPath } = await import(new URL('index.js', directory));
    assert.equal(libraryPath, fileURLToPath(new URL(`bin/${libraryName}`, directory)));
    assert.equal(addonPath, fileURLToPath(new URL('bin/doe_napi.node', directory)));
  }
  assert.equal(manifest.name, tuple.packageName);
  // The wrapper pins each platform package exactly; versions may differ.
  assert.equal(wrapper.optionalDependencies[tuple.packageName], manifest.version);
  assert.deepEqual(manifest.os, [platform]);
  assert.deepEqual(manifest.cpu, [arch]);
}
assert.deepEqual(Object.keys(wrapper.optionalDependencies).sort(),
  ['@clocksmith/doe-gpu-win32-x64', 'doe-gpu-darwin-arm64', 'doe-gpu-linux-x64']);
assert.equal(nativeInstallPlatform('win32', 'arm64'), null);
assert.equal(nativeInstallPlatform('unknown', 'x64'), null);
console.log('ok: local native installation tuples and dependency versions agree');
