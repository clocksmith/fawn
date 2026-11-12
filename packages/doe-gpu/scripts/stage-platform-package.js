#!/usr/bin/env node

// doe-gpu — stage a platform package bin payload from C native stage artifacts.

import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readFileSync,
  realpathSync,
  rmSync,
} from 'node:fs';
import { cStageRoot, cLibraryName, cRuntimeDirectory, cStageFiles } from './c-native-stage.js';
import { glibcFloorProblems } from './platform-staging.js';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const SUPPORTED_PLATFORMS = new Set(['darwin', 'linux', 'win32']);

function readJsonFile(path) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

function resolveExistingPath(candidates, label) {
  for (const candidate of candidates) {
    if (candidate && existsSync(candidate)) {
      return candidate;
    }
  }
  throw new Error(`doe-gpu: missing ${label}. Checked: ${candidates.filter(Boolean).join(', ')}`);
}

function currentHostTag() {
  return `${process.platform}-${process.arch}`;
}

function main() {
  for (const name of ['DOE_WEBGPU_LIB', 'DOE_LIB', 'DOE_NAPI_ADDON']) {
    if (process.env[name] && !existsSync(process.env[name])) {
      throw new Error(`doe-gpu: explicit ${name} does not exist: ${process.env[name]}`);
    }
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
    throw new Error(`doe-gpu: package "${packageJson.name}" must declare os/cpu for staging.`);
  }
  if (!SUPPORTED_PLATFORMS.has(targetPlatform)) {
    throw new Error(`doe-gpu: unsupported package platform "${targetPlatform}".`);
  }

  const workspaceRoot = resolve(targetPackageRoot, '..', '..');
  const targetHostTag = `${targetPlatform}-${targetArch}`;
  const expectedCurrentHost = currentHostTag();
  if (
    targetHostTag !== expectedCurrentHost
    && (!process.env.DOE_NAPI_ADDON || (!process.env.DOE_WEBGPU_LIB && !process.env.DOE_LIB))
  ) {
    throw new Error(
      `doe-gpu: ${packageJson.name} targets ${targetHostTag}, but the current host is ${expectedCurrentHost}. `
      + 'Provide DOE_NAPI_ADDON and DOE_WEBGPU_LIB/DOE_LIB from a matching C stage when staging cross-host packages.',
    );
  }

  const addonPath = resolveExistingPath([
    process.env.DOE_NAPI_ADDON,
    resolve(workspaceRoot, 'packages', 'doe-gpu', 'build', 'Release', 'doe_napi.node'),
    resolve(workspaceRoot, 'packages', 'doe-gpu', 'src', 'vendor', 'build', 'Release', 'doe_napi.node'),
  ], `${packageJson.name} addon`);
  const libraryPath = resolveExistingPath([
    process.env.DOE_WEBGPU_LIB,
    process.env.DOE_LIB,
    resolve(cStageRoot(workspaceRoot), cRuntimeDirectory(targetPlatform), cLibraryName(targetPlatform)),
  ], `${packageJson.name} C shared library`);
  if (!/(^|[\\/])(lib)?webgpu_doe_c\.(so|dylib|dll)$/.test(libraryPath)) {
    throw new Error(
      `doe-gpu: ${packageJson.name} staging requires a C stage library (*webgpu_doe_c.{so,dylib,dll}), got "${libraryPath}".`,
    );
  }

  const binRoot = resolve(targetPackageRoot, 'bin');
  const libraryDir = dirname(libraryPath);
  const { files, manifestPath, manifest } = cStageFiles(libraryPath, workspaceRoot);
  rmSync(binRoot, { recursive: true, force: true });
  mkdirSync(binRoot, { recursive: true });
  copyFileSync(addonPath, resolve(binRoot, 'doe_napi.node'));
  for (const file of files) {
    const target = resolve(binRoot, file.target);
    mkdirSync(dirname(target), { recursive: true });
    copyFileSync(file.source, target);
  }
  copyFileSync(manifestPath, resolve(binRoot, 'doe-c-manifest.json'));
  if (targetPlatform === 'linux' && manifest.glibc_floor) {
    const problems = glibcFloorProblems(binRoot, manifest.glibc_floor);
    if (problems.length > 0) {
      rmSync(binRoot, { recursive: true, force: true });
      throw new Error(`doe-gpu: ${packageJson.name} exceeds the C stage glibc floor: ${problems.join('; ')}`);
    }
  }
  console.log(`doe-gpu: staged ${packageJson.name} with the C runtime from ${dirname(libraryDir)}`);
}

if (process.argv[1] && realpathSync(resolve(process.argv[1])) === realpathSync(fileURLToPath(import.meta.url))) {
  main();
}
