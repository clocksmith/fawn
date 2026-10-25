import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { basename, dirname, resolve } from 'node:path';
import { libraryBasenamesForPlatform } from './platform-package.js';

const EXPECTED_SCHEMA_VERSION = 1;
const EXPECTED_ARTIFACT_TYPE = 'libwebgpu_doe';

export const UNKNOWN_DOE_BUILD_METADATA = Object.freeze({
  source: 'none',
  path: '',
  leanVerifiedBuild: null,
  proofArtifactSha256: null,
});

function isObject(value) {
  return value != null && typeof value === 'object' && !Array.isArray(value);
}

function normalizeProofArtifactSha256(value) {
  return value == null ? null : typeof value === 'string' ? value : undefined;
}

function parseDoeBuildSidecar(json, metadataPath, source) {
  if (!isObject(json)) return null;
  if (json.schemaVersion !== EXPECTED_SCHEMA_VERSION) return null;
  if (json.artifact !== EXPECTED_ARTIFACT_TYPE) return null;
  if (typeof json.leanVerifiedBuild !== 'boolean') return null;
  const proofArtifactSha256 = normalizeProofArtifactSha256(json.proofArtifactSha256);
  if (proofArtifactSha256 === undefined) return null;
  return {
    source,
    path: metadataPath,
    leanVerifiedBuild: json.leanVerifiedBuild,
    proofArtifactSha256,
  };
}

function parsePrebuildMetadata(json, metadataPath, source) {
  if (!isObject(json)) return null;
  if (json.schemaVersion !== EXPECTED_SCHEMA_VERSION) return null;
  const doeBuild = json.doeBuild;
  if (!isObject(doeBuild)) return null;
  if (doeBuild.artifact !== EXPECTED_ARTIFACT_TYPE) return null;
  if (typeof doeBuild.leanVerifiedBuild !== 'boolean') return null;
  const proofArtifactSha256 = normalizeProofArtifactSha256(doeBuild.proofArtifactSha256);
  if (proofArtifactSha256 === undefined) return null;
  return {
    source,
    path: metadataPath,
    leanVerifiedBuild: doeBuild.leanVerifiedBuild,
    proofArtifactSha256,
  };
}

function readJsonFile(metadataPath) {
  try {
    return JSON.parse(readFileSync(metadataPath, 'utf8'));
  } catch {
    return null;
  }
}

function metadataPathExists(metadataPath) {
  try {
    return existsSync(metadataPath);
  } catch {
    // Node's permission model throws for undeclared optional probes. Treat the
    // candidate as unavailable and continue to an explicitly readable sidecar.
    return false;
  }
}

export function readDoeBuildMetadataFile(metadataPath) {
  if (!metadataPath || !metadataPathExists(metadataPath)) return null;
  const parsed = readJsonFile(metadataPath);
  return parseDoeBuildSidecar(parsed, metadataPath, 'zig-out');
}

function loadMetadataCandidate(metadataPath, parser, source) {
  if (!metadataPath || !metadataPathExists(metadataPath)) return null;
  const parsed = readJsonFile(metadataPath);
  return parser(parsed, metadataPath, source);
}

const C_LIBRARY = /^(lib)?webgpu_doe_c\.(so|dylib|dll)$/;

function sha256File(path) {
  try {
    return createHash('sha256').update(readFileSync(path)).digest('hex');
  } catch {
    return null;
  }
}

function stringOrNull(value) {
  return typeof value === 'string' && value.length > 0 ? value : null;
}

// The C runtime's sidecar is its package stage MANIFEST.json (runtime/c/tools/
// build-native-package.py): next to the library as doe-c-manifest.json in the
// platform package, or the stage root's MANIFEST.json above lib/. A manifest
// describes the selected library only when its artifacts["lib/<name>"] digest
// equals that file's sha256. Its lean_proof record (from the same Lean inputs
// runtime/zig/build.zig reads) states whether the build is Lean-verified and its
// proof artifact digest; a manifest without that record leaves both null.
function loadDoeCManifest(libraryPath) {
  const libraryDir = dirname(libraryPath);
  const artifactKey = `${libraryPath.endsWith('.dll') ? 'bin' : 'lib'}/${basename(libraryPath)}`;
  let librarySha256;
  for (const [metadataPath, source] of [
    [resolve(libraryDir, 'doe-c-manifest.json'), 'c-package'],
    [resolve(libraryDir, '..', 'MANIFEST.json'), 'c-stage'],
  ]) {
    if (!metadataPathExists(metadataPath)) continue;
    const manifest = readJsonFile(metadataPath);
    const recorded = isObject(manifest) && isObject(manifest.artifacts) ? manifest.artifacts[artifactKey] : undefined;
    if (typeof recorded !== 'string') continue;
    librarySha256 ??= sha256File(libraryPath);
    if (librarySha256 === null || recorded !== librarySha256) continue;
    const lean = isObject(manifest.lean_proof) ? manifest.lean_proof : {};
    return {
      source,
      path: metadataPath,
      leanVerifiedBuild: typeof lean.leanVerifiedBuild === 'boolean' ? lean.leanVerifiedBuild : null,
      proofArtifactSha256: stringOrNull(lean.proofArtifactSha256),
      identity: {
        sourceCommit: stringOrNull(manifest.source_commit),
        sourceTree: stringOrNull(manifest.source_tree),
        artifactSha256: librarySha256,
        qualification: stringOrNull(manifest.qualification),
      },
    };
  }
  return UNKNOWN_DOE_BUILD_METADATA;
}

export function loadDoeBuildMetadata({ libraryPath = '' } = {}) {
  // The C runtime has its own manifest contract and never reads drop-in
  // metadata (including DOE_BUILD_METADATA).
  if (libraryPath && C_LIBRARY.test(basename(libraryPath))) {
    return loadDoeCManifest(libraryPath);
  }
  // These legacy sidecars describe only the drop-in artifact. A delegate or
  // unknown override has no matching metadata contract and must not inherit its proof.
  if (!libraryPath || !libraryBasenamesForPlatform().includes(basename(libraryPath))) {
    return UNKNOWN_DOE_BUILD_METADATA;
  }
  const seen = new Set();
  const candidates = [];

  const pushCandidate = (metadataPath, parser, source) => {
    if (!metadataPath || seen.has(metadataPath)) return;
    seen.add(metadataPath);
    candidates.push({ metadataPath, parser, source });
  };

  pushCandidate(process.env.DOE_BUILD_METADATA ?? '', parseDoeBuildSidecar, 'env');

  if (libraryPath) {
    const libraryDir = dirname(libraryPath);
    pushCandidate(resolve(libraryDir, 'metadata.json'), parsePrebuildMetadata, 'prebuild');
    pushCandidate(resolve(libraryDir, 'doe-build-metadata.json'), parseDoeBuildSidecar, 'adjacent');
    pushCandidate(resolve(libraryDir, '..', 'share', 'doe-build-metadata.json'), parseDoeBuildSidecar, 'workspace');
  }

  for (const candidate of candidates) {
    const parsed = loadMetadataCandidate(candidate.metadataPath, candidate.parser, candidate.source);
    if (parsed) return parsed;
  }

  return UNKNOWN_DOE_BUILD_METADATA;
}
