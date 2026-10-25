#!/usr/bin/env node

// Rebuild the committed C package before publishing; explicit cross-host artifacts
// still pass the same source/artifact manifest guard during staging.

import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { cStageRoot, cStageFiles, cLibraryName, cRuntimeDirectory } from './c-native-stage.js';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));

function readJsonFile(path) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

function main() {
  if (process.env.DOE_SKIP_NATIVE_REBUILD === '1') {
    console.warn(
      'doe-gpu: DOE_SKIP_NATIVE_REBUILD=1 — skipping C build; staging will still ' +
      'run the freshness guard in stage-platform-package.js. Never set this on publish.',
    );
    return;
  }

  const targetPackageRoot = process.cwd();
  const packageJsonPath = resolve(targetPackageRoot, 'package.json');
  if (!existsSync(packageJsonPath)) {
    throw new Error(`doe-gpu: missing package.json in "${targetPackageRoot}".`);
  }
  const packageJson = readJsonFile(packageJsonPath);
  const targetPlatform = Array.isArray(packageJson.os) ? packageJson.os[0] : null;
  const targetArch = Array.isArray(packageJson.cpu) ? packageJson.cpu[0] : null;
  if (!targetPlatform || !targetArch) {
    throw new Error(`doe-gpu: package "${packageJson.name}" must declare os/cpu for rebuild.`);
  }
  const targetHostTag = `${targetPlatform}-${targetArch}`;
  const currentHostTag = `${process.platform}-${process.arch}`;

  if (targetHostTag !== currentHostTag) {
    if (!process.env.DOE_NAPI_ADDON || (!process.env.DOE_WEBGPU_LIB && !process.env.DOE_LIB)) {
      throw new Error(
        `doe-gpu: ${packageJson.name} targets ${targetHostTag} but current host is ${currentHostTag}. ` +
        'Cross-host publish requires DOE_NAPI_ADDON and DOE_WEBGPU_LIB/DOE_LIB pointing at a build ' +
        'produced on a matching host; the freshness guard in stage will verify them against the current source.',
      );
    }
    console.log(
      `doe-gpu: ${packageJson.name}: cross-host publish detected (current=${currentHostTag}, ` +
      `target=${targetHostTag}). Skipping C rebuild; staging will use DOE_NAPI_ADDON/DOE_WEBGPU_LIB env overrides.`,
    );
    return;
  }

  const workspaceRoot = resolve(targetPackageRoot, '..', '..');
  const stage = cStageRoot(workspaceRoot);
  if (existsSync(stage)) {
    cStageFiles(resolve(stage, cRuntimeDirectory(), cLibraryName()), workspaceRoot);
    console.log(`doe-gpu: reusing source-verified C package ${stage}`);
    return;
  }
  const temporary = mkdtempSync(resolve(tmpdir(), 'doe-c-publish-'));
  const result = spawnSync(process.env.PYTHON ?? 'python3', [
    resolve(workspaceRoot, 'runtime/c/tools/build-native-package.py'),
    '--repo', workspaceRoot, '--ref', 'HEAD', '--build', resolve(temporary, 'build'),
    '--destination', stage, '--cc', process.env.CC ?? 'cc', '--cxx', process.env.CXX ?? 'c++',
    ...(process.platform === 'win32' ? ['--dxc-root', process.env.DXC_ROOT ?? '',
      '--clang-cl', process.env.CLANG_CL ?? 'clang-cl'] : []),
  ], { cwd: workspaceRoot, stdio: 'inherit', env: process.env });
  if (result.error || result.status !== 0) {
    throw new Error(`doe-gpu: C package rebuild failed: ${result.error?.message ?? result.status}; logs: ${temporary}`);
  }
  cStageFiles(resolve(stage, cRuntimeDirectory(), cLibraryName()), workspaceRoot);
}

main();
