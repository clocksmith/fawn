// doe-gpu — read npm package tarballs without unpacking them.
//
// Clean-install gates may install an exact retained tarball instead of packing
// the workspace; its identity and staged payload hashes come from its members.
import { readFileSync, statSync } from 'node:fs';
import { basename } from 'node:path';
import { gunzipSync } from 'node:zlib';

const BLOCK_BYTES = 512;

function headerString(header, offset, length) {
  const field = header.subarray(offset, offset + length);
  const end = field.indexOf(0);
  return field.subarray(0, end === -1 ? field.length : end).toString('utf8');
}

function paxPath(payload) {
  for (const record of payload.toString('utf8').split('\n')) {
    const match = /^\d+ path=(.*)$/u.exec(record);
    if (match) return match[1];
  }
  return null;
}

/** Regular-file members of a gzip tar archive, keyed by path. */
function tarballMembers(tarball) {
  const archive = gunzipSync(readFileSync(tarball));
  const members = new Map();
  let offset = 0;
  let pendingPath = null;
  while (offset + BLOCK_BYTES <= archive.length) {
    const header = archive.subarray(offset, offset + BLOCK_BYTES);
    if (header.every((byte) => byte === 0)) break;
    const size = Number.parseInt(headerString(header, 124, 12).trim() || '0', 8);
    const type = headerString(header, 156, 1) || '0';
    const prefix = headerString(header, 257, 6).startsWith('ustar')
      ? headerString(header, 345, 155) : '';
    const name = headerString(header, 0, 100);
    const payload = archive.subarray(offset + BLOCK_BYTES, offset + BLOCK_BYTES + size);
    if (type === 'x') {
      pendingPath = paxPath(payload);
    } else {
      const path = pendingPath ?? (prefix ? `${prefix}/${name}` : name);
      pendingPath = null;
      if (type === '0') {
        if (members.has(path)) throw new Error(`${tarball}: duplicate member ${path}`);
        members.set(path, Buffer.from(payload));
      }
    }
    offset += BLOCK_BYTES + Math.ceil(size / BLOCK_BYTES) * BLOCK_BYTES;
  }
  return members;
}

/** One required member's bytes; a missing member is an explicit failure. */
function tarballMember(tarball, name) {
  const payload = tarballMembers(tarball).get(name);
  if (!payload) throw new Error(`${tarball}: missing package member ${name}`);
  return payload;
}

/** The same shape npm pack --json reports, read from an existing tarball. */
function packedPackage(tarball) {
  const packageJson = JSON.parse(tarballMember(tarball, 'package/package.json').toString('utf8'));
  return {
    manifest: {
      id: `${packageJson.name}@${packageJson.version}`,
      name: packageJson.name,
      version: packageJson.version,
      size: statSync(tarball).size,
      filename: basename(tarball),
    },
    packageJson,
    tarball,
  };
}

/**
 * The wrapper pins every platform package through its optionalDependencies;
 * an install pairing other versions is not a candidate for that wrapper.
 */
function assertPinnedPlatform(wrapper, platform) {
  const pinned = wrapper.packageJson.optionalDependencies?.[platform.manifest.name];
  if (pinned !== platform.manifest.version) {
    throw new Error(
      `${wrapper.manifest.id} pins ${platform.manifest.name}@${pinned ?? '<absent>'}, `
      + `not the installed ${platform.manifest.id}`,
    );
  }
}

export { tarballMembers, tarballMember, packedPackage, assertPinnedPlatform };
