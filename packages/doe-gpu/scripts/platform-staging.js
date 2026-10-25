// doe-gpu — freshness of a staged platform package against the workspace build.
//
// The clean-install integration tests install packages/doe-gpu-<platform>/bin,
// a gitignored copy of the workspace addon and Doe library. A copy older than
// the workspace build makes those tests fail with misleading errors, so they
// compare content hashes first and stop with an actionable message.

import { cStageRoot, cLibraryName, cRuntimeDirectory, cStageFiles } from './c-native-stage.js';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const ADDON_SOURCE_STAMP = 'doe_napi.sources.json';

function sha256File(path) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

// Hash of everything the addon is compiled from: the binding.gyp sources and
// every header in the bridge directory, in sorted path order.
function addonSourceSha256(packageRoot, workspaceRoot) {
  const binding = JSON.parse(readFileSync(resolve(packageRoot, 'binding.gyp'), 'utf8'));
  const target = binding.targets.find((entry) => entry.target_name === 'doe_napi');
  const bridgeRoot = resolve(workspaceRoot, 'runtime', 'bridge', 'webgpu-addon');
  const files = [
    ...target.sources.map((name) => resolve(packageRoot, name)),
    ...readdirSync(bridgeRoot).filter((name) => name.endsWith('.h')).map((name) => resolve(bridgeRoot, name)),
  ].sort();
  const hash = createHash('sha256');
  for (const file of files) {
    hash.update(file.slice(workspaceRoot.length));
    hash.update('\n');
    hash.update(readFileSync(file));
    hash.update('\n');
  }
  return hash.digest('hex');
}

// Written by build-addon.js beside the addon it compiled.
function writeAddonSourceStamp(outputDir, packageRoot, workspaceRoot) {
  writeFileSync(
    resolve(outputDir, ADDON_SOURCE_STAMP),
    `${JSON.stringify({ schemaVersion: 1, sourceSha256: addonSourceSha256(packageRoot, workspaceRoot) }, null, 2)}\n`,
  );
}

// Problems that make a staged platform package stale, as messages. Staged
// files are compared by content with the workspace artifacts they were copied
// from; an artifact the workspace does not have is not compared.
function stagedPlatformProblems({ platformPackageRoot, packageRoot, workspaceRoot, libraryFilename, provider = 'c' }) {
  const binRoot = resolve(platformPackageRoot, 'bin');
  const cName = cLibraryName();
  if (!['c', 'zig'].includes(provider)) throw new Error(`Unsupported staging provider: ${provider}`);
  const cStage = provider === 'c' ? cStageRoot(workspaceRoot) : resolve(workspaceRoot, 'runtime/zig/zig-out');
  const cLibrary = resolve(cStage, cRuntimeDirectory(), cName);
  const pairs = [
    ['addon', 'doe_napi.node', resolve(packageRoot, 'build', 'Release', 'doe_napi.node')],
    ['library', provider === 'c' ? cName : libraryFilename, provider === 'c' ? cLibrary : resolve(cStage, 'lib', libraryFilename)],
    ['build metadata', provider === 'c' ? 'doe-c-manifest.json' : 'doe-build-metadata.json',
      resolve(cStage, provider === 'c' ? 'MANIFEST.json' : 'share/doe-build-metadata.json')],
  ];
  const problems = [];
  if (provider === 'c' && existsSync(cLibrary)) {
    try {
      const { files } = cStageFiles(cLibrary, workspaceRoot);
      for (const file of files) if (file.source !== cLibrary) pairs.push(['C dependency', file.target, file.source]);
    } catch (error) { problems.push(error.message); }
  }
  for (const [label, stagedName, workspacePath] of pairs) {
    const stagedPath = resolve(binRoot, stagedName);
    if (!existsSync(stagedPath)) {
      problems.push(`the staged ${label} ${stagedPath} is missing`);
    } else if (existsSync(workspacePath) && sha256File(stagedPath) !== sha256File(workspacePath)) {
      problems.push(`the staged ${label} differs from the workspace build ${workspacePath}`);
    }
  }
  const stampPath = resolve(packageRoot, 'build', 'Release', ADDON_SOURCE_STAMP);
  if (existsSync(stampPath)) {
    const stamp = JSON.parse(readFileSync(stampPath, 'utf8'));
    if (stamp.sourceSha256 !== addonSourceSha256(packageRoot, workspaceRoot)) {
      problems.push('the workspace addon predates its sources (run npm run build:addon in packages/doe-gpu first)');
    }
  }
  return problems;
}

function assertStagedPlatformFresh(options) {
  const problems = stagedPlatformProblems(options);
  if (problems.length > 0) {
    throw new Error(
      `doe-gpu: the staged platform package ${options.platformPackageRoot} is stale: ${problems.join('; ')}. `
      + 'Restage it with `node ../doe-gpu/scripts/stage-platform-package.js` run in that package directory.',
    );
  }
}

export {
  addonSourceSha256,
  writeAddonSourceStamp,
  stagedPlatformProblems,
  assertStagedPlatformFresh,
};
