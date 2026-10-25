#!/usr/bin/env node

// doe-gpu — stage package-scoped native prebuilds for npm pack/publish.

import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { cStageRoot, cLibraryName, cRuntimeDirectory, cStageFiles } from './c-native-stage.js';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const LIB_EXT = Object.freeze({
  darwin: 'dylib',
  linux: 'so',
  win32: 'dll',
});

const __dirname = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(__dirname, '..');
const WORKSPACE_ROOT = resolve(PACKAGE_ROOT, '..', '..');
const PLATFORM_TAG = `${process.platform}-${process.arch}`;
const PREBUILD_ROOT = resolve(PACKAGE_ROOT, 'prebuilds', PLATFORM_TAG);

function resolveLibraryExtension() {
  const ext = LIB_EXT[process.platform];
  if (!ext) {
    throw new Error(`doe-gpu: unsupported platform "${process.platform}" for prebuild staging.`);
  }
  return ext;
}

function resolveExistingPath(candidates, label) {
  for (const candidate of candidates) {
    if (candidate && existsSync(candidate)) {
      return candidate;
    }
  }
  const checked = candidates.filter(Boolean).join(', ');
  throw new Error(`doe-gpu: missing ${label}. Checked: ${checked}`);
}

function readDoeBuildSidecar(metadataPath) {
  let parsed;
  try {
    parsed = JSON.parse(readFileSync(metadataPath, 'utf8'));
  } catch (error) {
    throw new Error(`doe-gpu: could not read build metadata "${metadataPath}": ${error.message}`);
  }
  if (parsed?.schemaVersion !== 1 || parsed?.artifact !== 'libwebgpu_doe') {
    throw new Error(`doe-gpu: unsupported build metadata payload at "${metadataPath}".`);
  }
  if (typeof parsed.leanVerifiedBuild !== 'boolean') {
    throw new Error(`doe-gpu: build metadata at "${metadataPath}" is missing leanVerifiedBuild.`);
  }
  if (parsed.proofArtifactSha256 != null && typeof parsed.proofArtifactSha256 !== 'string') {
    throw new Error(`doe-gpu: build metadata at "${metadataPath}" has invalid proofArtifactSha256.`);
  }
  return parsed;
}

function writePrebuildMetadata(sidecar, targetPath) {
  const payload = {
    schemaVersion: 1,
    doeBuild: {
      artifact: sidecar.artifact,
      leanVerifiedBuild: sidecar.leanVerifiedBuild,
      proofArtifactSha256: sidecar.proofArtifactSha256 ?? null,
    },
  };
  writeFileSync(targetPath, `${JSON.stringify(payload, null, 2)}\n`);
}

function copyRequiredFile(sourcePath, targetPath, label) {
  mkdirSync(dirname(targetPath), { recursive: true });
  copyFileSync(sourcePath, targetPath);
  console.log(`doe-gpu: staged ${label} -> ${targetPath}`);
}

function main() {
  for (const name of ['DOE_WEBGPU_LIB', 'DOE_LIB', 'DOE_NAPI_ADDON', 'DOE_BUILD_METADATA']) {
    if (process.env[name] && !existsSync(process.env[name])) {
      throw new Error(`doe-gpu: explicit ${name} does not exist: ${process.env[name]}`);
    }
  }
  const libraryExt = resolveLibraryExtension();
  const libraryPath = resolveExistingPath([
    process.env.DOE_WEBGPU_LIB,
    process.env.DOE_LIB,
    resolve(cStageRoot(WORKSPACE_ROOT), cRuntimeDirectory(), cLibraryName()),
  ], 'Doe shared library');
  const addonPath = resolveExistingPath([
    process.env.DOE_NAPI_ADDON,
    resolve(PACKAGE_ROOT, 'build', 'Release', 'doe_napi.node'),
    resolve(PACKAGE_ROOT, 'src', 'vendor', 'build', 'Release', 'doe_napi.node'),
  ], 'Doe N-API addon');
  if (/(^|[\\/])(lib)?webgpu_doe_c\.(so|dylib|dll)$/.test(libraryPath)) {
    const { files, manifestPath } = cStageFiles(libraryPath, WORKSPACE_ROOT);
    rmSync(PREBUILD_ROOT, { recursive: true, force: true });
    mkdirSync(PREBUILD_ROOT, { recursive: true });
    copyRequiredFile(addonPath, resolve(PREBUILD_ROOT, 'doe_napi.node'), 'native addon');
    for (const file of files) copyRequiredFile(file.source, resolve(PREBUILD_ROOT, file.target), 'C package dependency');
    copyRequiredFile(manifestPath, resolve(PREBUILD_ROOT, 'doe-c-manifest.json'), 'C manifest');
    return;
  }
  const metadataPath = resolveExistingPath([
    process.env.DOE_BUILD_METADATA,
    resolve(WORKSPACE_ROOT, 'runtime', 'zig', 'zig-out', 'share', 'doe-build-metadata.json'),
    resolve(dirname(libraryPath), '..', 'share', 'doe-build-metadata.json'),
  ], 'Doe build metadata');
  const buildMetadata = readDoeBuildSidecar(metadataPath);

  rmSync(PREBUILD_ROOT, { recursive: true, force: true });
  mkdirSync(PREBUILD_ROOT, { recursive: true });

  copyRequiredFile(addonPath, resolve(PREBUILD_ROOT, 'doe_napi.node'), 'native addon');
  copyRequiredFile(libraryPath, resolve(PREBUILD_ROOT, `libwebgpu_doe.${libraryExt}`), 'shared library');
  copyRequiredFile(
    metadataPath,
    resolve(PREBUILD_ROOT, 'doe-build-metadata.json'),
    'build metadata sidecar',
  );
  writePrebuildMetadata(buildMetadata, resolve(PREBUILD_ROOT, 'metadata.json'));
  console.log(`doe-gpu: staged prebuilds for ${PLATFORM_TAG}`);
}

main();
