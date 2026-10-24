#!/usr/bin/env node
// The C runtime installs through the package: a temporary copy of the Linux
// platform package is staged from a runtime/c package stage (DOE_C_STAGE=<stage>),
// both packages are packed and installed offline into a clean project, and a
// compute kernel there resolves libwebgpu_doe_c from the installed platform
// package (no DOE_WEBGPU_LIB) and returns exact results; providerInfo() reports
// the stage manifest's identity for that exact library. Skips without
// DOE_C_STAGE; the workspace staging and the Zig default are untouched.

import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = resolve(here, '../..');
const stage = process.env.DOE_C_STAGE;
if (!stage) {
  console.log('C package install: skipped (set DOE_C_STAGE to a runtime/c package stage)');
  process.exit(0);
}
if (`${process.platform}-${process.arch}` !== 'linux-x64') {
  console.log(`C package install: skipped (no platform package contract for ${process.platform}-${process.arch})`);
  process.exit(0);
}
const library = resolve(stage, 'lib', 'libwebgpu_doe_c.so');
const addon = resolve(packageRoot, 'build', 'Release', 'doe_napi.node');
assert.ok(existsSync(library), `missing ${library}`);
assert.ok(existsSync(addon), `missing ${addon}; run npm run build:addon`);

const scratch = mkdtempSync(join(tmpdir(), 'doe-c-package-install-'));
const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
const env = { ...process.env };
delete env.DOE_WEBGPU_LIB;
delete env.DOE_LIB;
function run(command, args, cwd, extraEnv = {}) {
  const result = spawnSync(command, args, { cwd, env: { ...env, ...extraEnv }, encoding: 'utf8' });
  assert.equal(result.status, 0, `${command} ${args.join(' ')} failed:\n${result.stdout}\n${result.stderr}`);
  return result.stdout;
}

try {
  const platformRoot = join(scratch, 'doe-gpu-linux-x64');
  mkdirSync(platformRoot);
  for (const name of ['package.json', 'index.js', 'LICENSE']) {
    const source = resolve(packageRoot, '..', 'doe-gpu-linux-x64', name);
    if (existsSync(source)) copyFileSync(source, join(platformRoot, name));
  }
  run(process.execPath, [resolve(packageRoot, 'scripts', 'stage-platform-package.js')], platformRoot,
    { DOE_WEBGPU_LIB: library, DOE_NAPI_ADDON: addon });
  const staged = readdirSync(join(platformRoot, 'bin')).sort();
  assert.deepEqual(staged, ['doe-c-manifest.json', 'doe_napi.node', 'libdoe_compiler.so', 'libwebgpu_doe_c.so']);

  const tarballs = join(scratch, 'tarballs');
  mkdirSync(tarballs);
  run(npm, ['pack', '--ignore-scripts', '--pack-destination', tarballs, '-q'], platformRoot);
  run(npm, ['pack', '--ignore-scripts', '--pack-destination', tarballs, '-q'], packageRoot);
  const app = join(scratch, 'app');
  mkdirSync(app);
  writeFileSync(join(app, 'package.json'), JSON.stringify({ name: 'c-install-check', version: '1.0.0', type: 'module', private: true }));
  run(npm, ['install', '--offline', '--no-audit', '--no-fund', '-q',
    ...readdirSync(tarballs).map((name) => join(tarballs, name))], app);

  writeFileSync(join(app, 'check.mjs'), `
import { create, setupGlobals, providerInfo } from 'doe-gpu';
setupGlobals(globalThis);
const info = providerInfo();
const adapter = await create().requestAdapter() ?? await create().requestAdapter({ forceFallbackAdapter: true });
if (!adapter) { console.log(JSON.stringify({ info, skipped: 'no adapter' })); process.exit(0); }
const device = await adapter.requestDevice();
const module = device.createShaderModule({ code: '@group(0) @binding(0) var<storage, read_write> d: array<u32>; @compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3u) { d[g.x] = d[g.x] * 2u + 1u; }' });
const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'main' } });
const n = 256;
const data = device.createBuffer({ size: n * 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
device.queue.writeBuffer(data, 0, new Uint32Array(n).map((_, i) => i));
const readback = device.createBuffer({ size: n * 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
const encoder = device.createCommandEncoder();
const pass = encoder.beginComputePass();
pass.setPipeline(pipeline);
pass.setBindGroup(0, device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: data } }] }));
pass.dispatchWorkgroups(n / 64);
pass.end();
encoder.copyBufferToBuffer(data, 0, readback, 0, n * 4);
device.queue.submit([encoder.finish()]);
await readback.mapAsync(GPUMapMode.READ);
const values = new Uint32Array(readback.getMappedRange());
console.log(JSON.stringify({ info, exact: values.every((v, i) => v === i * 2 + 1) }));
device.destroy();
`);
  const report = JSON.parse(run(process.execPath, ['check.mjs'], app).trim().split('\n').pop());
  assert.equal(report.info.libraryFlavor, 'doe-c', 'the installed package resolves the C runtime');
  assert.ok(report.info.doeLibraryPath.startsWith(join(app, 'node_modules', 'doe-gpu-linux-x64', 'bin')),
    `library comes from the installed platform package: ${report.info.doeLibraryPath}`);
  assert.equal(report.info.loaded, true, report.info.loadError);
  // Provenance is the stage manifest staged beside the library, bound to its digest.
  const stageManifest = JSON.parse(readFileSync(resolve(stage, 'MANIFEST.json'), 'utf8'));
  assert.equal(report.info.buildMetadataSource, 'c-package');
  assert.equal(report.info.buildMetadataPath, join(dirname(report.info.doeLibraryPath), 'doe-c-manifest.json'));
  assert.equal(report.info.leanVerifiedBuild, null, 'the C manifest records no Lean-verified build');
  assert.equal(report.info.proofArtifactSha256, null, 'the C manifest records no proof artifact');
  assert.deepEqual(report.info.buildIdentity, {
    sourceCommit: stageManifest.source_commit,
    sourceTree: stageManifest.source_tree,
    artifactSha256: stageManifest.artifacts['lib/libwebgpu_doe_c.so'],
    qualification: stageManifest.qualification,
  });
  if (report.skipped) {
    console.log(`C package install: installed and loaded; ${report.skipped}`);
  } else {
    assert.equal(report.exact, true, 'compute through the installed C runtime is exact');
    console.log('ok: the C runtime installs through the package and computes exactly');
  }
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
