// Retained package tarballs are read by member and pinned by the wrapper.
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { gzipSync } from 'node:zlib';
import {
  assertPinnedPlatform,
  packedPackage,
  tarballMember,
  tarballMembers,
} from '../lib/package-tarball.js';

function header(name, size, type) {
  const block = Buffer.alloc(512);
  block.write(name, 0, 100, 'utf8');
  block.write('0000644\0', 100);
  block.write(`${size.toString(8).padStart(11, '0')}\0`, 124);
  block.write('00000000000\0', 136);
  block.write(type, 156);
  block.write('ustar\0', 257);
  block.write('00', 263);
  block.fill(' ', 148, 156);
  let checksum = 0;
  for (const byte of block) checksum += byte;
  block.write(`${checksum.toString(8).padStart(6, '0')}\0 `, 148);
  return block;
}

function entry(name, payload, type = '0') {
  const padded = Buffer.alloc(Math.ceil(payload.length / 512) * 512);
  payload.copy(padded);
  return [header(name, payload.length, type), padded];
}

function tarball(path, members) {
  const blocks = [];
  for (const [name, payload] of Object.entries(members)) {
    if (name.length > 100) {
      const record = ` path=${name}\n`;
      let length = record.length;
      length += String(length + String(length).length).length;
      blocks.push(...entry('PaxHeader', Buffer.from(`${length}${record}`), 'x'));
      blocks.push(...entry(name.slice(0, 100), payload));
    } else {
      blocks.push(...entry(name, payload));
    }
  }
  blocks.push(Buffer.alloc(1024));
  writeFileSync(path, gzipSync(Buffer.concat(blocks)));
}

const directory = mkdtempSync(join(tmpdir(), 'doe-package-tarball-'));
try {
  const longName = `package/bin/licenses/${'x'.repeat(100)}.txt`;
  const wrapperPath = join(directory, 'doe-gpu-0.5.1.tgz');
  const platformPath = join(directory, 'clocksmith-doe-gpu-win32-x64-0.5.0.tgz');
  tarball(wrapperPath, {
    'package/package.json': Buffer.from(JSON.stringify({
      name: 'doe-gpu',
      version: '0.5.1',
      optionalDependencies: { '@clocksmith/doe-gpu-win32-x64': '0.5.0' },
    })),
  });
  tarball(platformPath, {
    'package/package.json': Buffer.from(JSON.stringify({
      name: '@clocksmith/doe-gpu-win32-x64', version: '0.5.0',
    })),
    'package/bin/webgpu_doe_c.dll': Buffer.from('dll'),
    [longName]: Buffer.from('terms'),
  });
  const wrapper = packedPackage(wrapperPath);
  const platform = packedPackage(platformPath);
  assert.equal(wrapper.manifest.id, 'doe-gpu@0.5.1');
  assert.equal(platform.manifest.id, '@clocksmith/doe-gpu-win32-x64@0.5.0');
  assert.equal(platform.manifest.filename, 'clocksmith-doe-gpu-win32-x64-0.5.0.tgz');
  assert.equal(tarballMember(platformPath, 'package/bin/webgpu_doe_c.dll').toString(), 'dll');
  assert.equal(tarballMembers(platformPath).get(longName).toString(), 'terms');
  assert.throws(() => tarballMember(platformPath, 'package/bin/missing.dll'), /missing package member/u);
  assertPinnedPlatform(wrapper, platform);
  const unpinned = { ...platform, manifest: { ...platform.manifest, version: '0.5.1',
    id: '@clocksmith/doe-gpu-win32-x64@0.5.1' } };
  assert.throws(() => assertPinnedPlatform(wrapper, unpinned), /pins @clocksmith\/doe-gpu-win32-x64@0\.5\.0/u);
  const unscoped = { ...platform, manifest: { ...platform.manifest, name: 'doe-gpu-win32-x64' } };
  assert.throws(() => assertPinnedPlatform(wrapper, unscoped), /<absent>/u);
} finally {
  rmSync(directory, { recursive: true, force: true });
}
console.log('ok: package tarball members and wrapper platform pins');
