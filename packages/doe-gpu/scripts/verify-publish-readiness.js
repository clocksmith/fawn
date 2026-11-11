#!/usr/bin/env node

import { spawnSync } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PLATFORM_PACKAGE_NAMES } from '../src/vendor/webgpu/platform-package.js';

const packageRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const registry = process.env.npm_config_registry || 'https://registry.npmjs.org/';

function runNpm(args) {
  const command = process.platform === 'win32' ? 'npm.cmd' : 'npm';
  const result = spawnSync(command, [...args, `--registry=${registry}`], {
    cwd: packageRoot,
    encoding: 'utf8',
  });
  if (result.status !== 0) {
    throw new Error((result.stderr || result.stdout || `npm exited ${result.status}`).trim());
  }
  return result.stdout.trim();
}

function normalizeConstraint(value) {
  if (Array.isArray(value)) return value;
  return value == null ? [] : [value];
}

// Each admitted platform package name maps to the tuple its registry os/cpu must declare.
const PLATFORM_TUPLES = new Map(Object.entries(PLATFORM_PACKAGE_NAMES).flatMap(([tag, names]) => {
  const separator = tag.lastIndexOf('-');
  return names.map((name) => [name, { os: tag.slice(0, separator), cpu: tag.slice(separator + 1) }]);
}));
const EXACT_VERSION = /^\d+\.\d+\.\d+$/u;

const packageJson = JSON.parse(await fs.readFile(path.join(packageRoot, 'package.json'), 'utf8'));
const account = runNpm(['whoami']);
if (!account) throw new Error('npm authentication returned an empty account name.');

const verifiedPlatforms = [];
for (const [name, version] of Object.entries(packageJson.optionalDependencies ?? {})) {
  // The wrapper pins each platform package exactly; a platform package keeps
  // its own published version when only the wrapper changes.
  const tuple = PLATFORM_TUPLES.get(name);
  if (!tuple) {
    throw new Error(`${name} is not a platform package name the loader resolves.`);
  }
  if (!EXACT_VERSION.test(version)) {
    throw new Error(`${name} must be pinned to an exact version; found ${version}.`);
  }
  const metadata = JSON.parse(
    runNpm(['view', `${name}@${version}`, 'version', 'dist.integrity', 'cpu', 'os', '--json']),
  );
  if (metadata.version !== version || typeof metadata['dist.integrity'] !== 'string') {
    throw new Error(`${name}@${version} is not a complete published platform package.`);
  }
  if (
    !normalizeConstraint(metadata.os).includes(tuple.os)
    || !normalizeConstraint(metadata.cpu).includes(tuple.cpu)
  ) {
    throw new Error(`${name}@${version} registry cpu/os metadata does not match ${tuple.os}-${tuple.cpu}.`);
  }
  verifiedPlatforms.push({ name, version, integrity: metadata['dist.integrity'] });
}

console.log(JSON.stringify({
  ok: true,
  schema: 'doe.npm-publish-preflight/v1',
  account,
  registry,
  package: { name: packageJson.name, version: packageJson.version },
  verifiedPlatforms,
}, null, 2));
