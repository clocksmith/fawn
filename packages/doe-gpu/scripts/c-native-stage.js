// Canonical C package owner supplies source and artifact hashes; do not invent
// Zig compiler provenance or treat a sidecar alone as proof of library identity.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { basename, dirname, resolve, sep } from 'node:path';

function fileHash(path) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}
function cStageRoot(workspaceRoot) {
  if (process.env.DOE_C_STAGE) return resolve(process.env.DOE_C_STAGE);
  const revision = spawnSync('git', ['rev-parse', '--verify', 'HEAD'], { cwd: workspaceRoot, encoding: 'utf8' });
  if (revision.status !== 0) throw new Error('Cannot resolve committed C package revision');
  return resolve(workspaceRoot, '.cache/doe-c-packages', revision.stdout.trim());
}
function cLibraryName(platform = process.platform) {
  const names = { linux: 'libwebgpu_doe_c.so', darwin: 'libwebgpu_doe_c.dylib', win32: 'webgpu_doe_c.dll' };
  if (!names[platform]) throw new Error(`Unsupported C package platform: ${platform}`);
  return names[platform];
}
function cRuntimeDirectory(platform = process.platform) { return platform === 'win32' ? 'bin' : 'lib'; }
function cStageFiles(libraryPath, workspaceRoot) {
  const directory = dirname(libraryPath);
  const stage = resolve(directory, '..');
  const manifestPath = resolve(stage, 'MANIFEST.json');
  const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
  if (!manifest.source_inputs || !Object.keys(manifest.source_inputs).length || !manifest.artifacts || !manifest.source_commit) {
    throw new Error(`Missing C source/artifact provenance: ${manifestPath}`);
  }
  const inventory = spawnSync('git', ['ls-files', '-z', '--', 'runtime/c'], { cwd: workspaceRoot, encoding: 'utf8' });
  if (inventory.status !== 0) throw new Error('Cannot verify current C source inventory');
  for (const name of inventory.stdout.split('\0').filter(Boolean)) {
    if (!(name in manifest.source_inputs)) throw new Error(`C package omits current source: ${name}`);
  }
  for (const [name, hash] of Object.entries(manifest.source_inputs)) {
    const source = resolve(workspaceRoot, name);
    if (!source.startsWith(resolve(workspaceRoot) + sep) || !existsSync(source) || fileHash(source) !== hash) {
      throw new Error(`C package predates or differs from source: ${name}; rebuild the committed C package`);
    }
  }
  const extension = libraryPath.slice(libraryPath.lastIndexOf('.'));
  const compiler = ['libdoe_compiler', 'doe_compiler'].map((name) => resolve(directory, name + extension))
    .find((path) => existsSync(path));
  if (!compiler) throw new Error(`Missing sibling C compiler library: ${directory}`);
  const windows = extension === '.dll';
  const runtimeDirectory = windows ? 'bin' : 'lib';
  const runtimePaths = [libraryPath, compiler,
    ...(windows ? ['dxcompiler.dll', 'dxil.dll'].map((name) => resolve(directory, name)) : [])];
  const files = runtimePaths.map((path) => ({ source: path, target: basename(path), key: `${runtimeDirectory}/${basename(path)}` }));
  for (const key of Object.keys(manifest.artifacts)) {
    if (key.startsWith('licenses/') || key === 'NATIVE_BUILD.json') files.push({ source: resolve(stage, key), target: key, key });
  }
  if (windows && (!files.some((file) => file.key.startsWith('licenses/DXC/')) || !manifest.artifacts['NATIVE_BUILD.json'])) {
    throw new Error(`Missing Windows DXC license/build provenance: ${manifestPath}`);
  }
  for (const file of files) {
    if (!file.source.startsWith(stage + sep) || !existsSync(file.source) || manifest.artifacts[file.key] !== fileHash(file.source)) {
      throw new Error(`C package artifact digest mismatch: ${file.source}`);
    }
  }
  return { libraryPath, compilerPath: compiler, manifestPath, manifest, files };

}
export { cStageRoot, cLibraryName, cRuntimeDirectory, cStageFiles };
