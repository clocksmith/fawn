// Installed provider admission keeps C manifest identity distinct from Zig metadata.
import { createHash } from 'node:crypto';
import { readFileSync, realpathSync } from 'node:fs';
import { basename, dirname, extname, isAbsolute, relative, resolve, sep } from 'node:path';

const SHA256 = /^[a-f0-9]{64}$/u;
const GIT_OBJECT = /^[a-f0-9]{40}$/u;

function inside(root, file) {
  const distance = relative(root, file);
  return distance !== '..' && !distance.startsWith(`..${sep}`) && !isAbsolute(distance);
}

function sha256(file) {
  return createHash('sha256').update(readFileSync(file)).digest('hex');
}

function assertInstalledNativeProvider(provider, platformRoot) {
  if (provider?.loaded !== true || provider?.doeNative !== true
      || typeof provider.doeLibraryPath !== 'string') {
    throw new Error('installed provider is not a loaded native runtime');
  }
  const root = realpathSync.native(platformRoot);
  const library = realpathSync.native(provider.doeLibraryPath);
  if (!inside(root, library)) throw new Error(`native library escaped the installed platform package: root=${root} library=${library}`);
  const filename = basename(library);
  if (!/^(?:lib)?webgpu_doe_c\.(?:so|dylib|dll)$/u.test(filename)) {
    if (provider.buildMetadataSource !== 'prebuild') throw new Error('installed Zig provider lacks prebuild metadata');
    return { source: 'prebuild', libraryPath: library };
  }
  if (provider.libraryFlavor !== 'doe-c' || provider.buildMetadataSource !== 'c-package') {
    throw new Error('installed C provider requires its C package manifest identity');
  }
  if (typeof provider.buildMetadataPath !== 'string') throw new Error('C package manifest path is missing');
  const manifestPath = realpathSync.native(provider.buildMetadataPath);
  if (!inside(root, manifestPath) || manifestPath !== resolve(dirname(library), 'doe-c-manifest.json')) {
    throw new Error('C manifest escaped or differs from the installed library sibling');
  }
  const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
  const identity = provider.buildIdentity;
  if (!GIT_OBJECT.test(manifest.source_commit) || !GIT_OBJECT.test(manifest.source_tree)
      || identity?.sourceCommit !== manifest.source_commit || identity?.sourceTree !== manifest.source_tree) {
    throw new Error('C provider source identity differs from its manifest');
  }
  if (!manifest.source_inputs || typeof manifest.source_inputs !== 'object' || Array.isArray(manifest.source_inputs)) {
    throw new Error('C manifest source inputs must be an object');
  }
  const sources = Object.entries(manifest.source_inputs);
  if (sources.length === 0 || sources.some(([name, hash]) => isAbsolute(name)
      || name.split(/[\\/]/u).includes('..') || !SHA256.test(hash))) {
    throw new Error('C manifest lacks valid source input hashes');
  }
  const extension = extname(library);
  const directory = extension === '.dll' ? 'bin' : 'lib';
  const artifactKey = `${directory}/${filename}`;
  const libraryHash = sha256(library);
  if (manifest.artifacts?.[artifactKey] !== libraryHash || identity.artifactSha256 !== libraryHash) {
    throw new Error('installed C library hash differs from its manifest identity');
  }
  const dependencies = Object.entries(manifest.artifacts).filter(([name]) => (
    name.startsWith(`${directory}/`) && extname(name) === extension
  ));
  if (!dependencies.some(([name]) => /\/(?:lib)?doe_compiler\.(?:so|dylib|dll)$/u.test(name))) {
    throw new Error('C manifest omits its compiler dependency');
  }
  for (const [name, hash] of dependencies) {
    if (name.split('/').length !== 2 || !SHA256.test(hash)) throw new Error('invalid C runtime artifact entry');
    const dependency = realpathSync.native(resolve(dirname(library), basename(name)));
    if (!inside(root, dependency) || sha256(dependency) !== hash) {
      throw new Error('installed C dependency hash differs from its manifest');
    }
  }
  return { source: 'c-package', libraryPath: library, manifestPath,
    manifestSha256: sha256(manifestPath), librarySha256: libraryHash,
    sourceCommit: manifest.source_commit, sourceTree: manifest.source_tree };
}

export { assertInstalledNativeProvider };
