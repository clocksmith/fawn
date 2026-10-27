// Package dependency versions and local installed-runtime tuples agree.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { nativeInstallPlatform } from '../lib/native-install-platform.js';

const wrapper = JSON.parse(readFileSync(new URL('../../package.json', import.meta.url)));
for (const [platform, arch, libraryName] of [
  ['linux', 'x64', 'libwebgpu_doe_c.so'],
  ['darwin', 'arm64', 'libwebgpu_doe_c.dylib'],
  ['win32', 'x64', 'webgpu_doe_c.dll'],
]) {
  const tuple = nativeInstallPlatform(platform, arch);
  assert.equal(tuple.libraryName, libraryName);
  const manifest = JSON.parse(readFileSync(new URL(`../../../${tuple.packageName}/package.json`, import.meta.url)));
  assert.equal(wrapper.optionalDependencies[tuple.packageName], manifest.version);
  assert.equal(manifest.version, wrapper.version);
  assert.deepEqual(manifest.os, [platform]);
  assert.deepEqual(manifest.cpu, [arch]);
}
assert.equal(nativeInstallPlatform('win32', 'arm64'), null);
assert.equal(nativeInstallPlatform('unknown', 'x64'), null);
console.log('ok: local native installation tuples and dependency versions agree');
