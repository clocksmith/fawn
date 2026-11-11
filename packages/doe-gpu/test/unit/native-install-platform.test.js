// Package dependency versions and local installed-runtime tuples agree.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { nativeInstallPlatform } from '../lib/native-install-platform.js';
import { platformPackageNames } from '../../src/vendor/webgpu/platform-package.js';

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
