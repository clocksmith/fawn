import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { NativeFrameHelper } from '../../src/adapters/native-frame-helper.js';

const helper = new NativeFrameHelper(), width = 130, height = 65;
try {
  const pixels = Buffer.alloc(width * height * 4, 0x77);
  const sample = verify => helper.sample(width, height, pixels.toString('base64'), verify);
  const first = await sample(false), same = await sample(false);
  assert.equal(first.hashes.length, 6);
  assert.equal(first.hashedBytes, pixels.length);
  assert.equal(same.hashedBytes, 0);
  pixels[pixels.length - 1] = 0x88;
  const rotated = await sample(false);
  assert.deepEqual(rotated.hashes.slice(0, -1), first.hashes.slice(0, -1));
  assert.equal(rotated.hashedBytes, 8);
  assert.equal(rotated.hashes.at(-1), createHash('sha256').update(pixels.subarray(-8)).digest('hex'));
  assert.equal((await sample(true)).hashedBytes, pixels.length);
  assert.equal((await helper.sample(65,130,pixels.toString('base64'),false)).hashedBytes, pixels.length);
  await assert.rejects(helper.sample(4097,1024,'',false), /dimensions/);
  await assert.rejects(helper.sample(1,1,'$$$$$$$$',false), /encoding/);
} finally { await helper.close(); }
await assert.rejects(helper.sample(1,1,'AAAAAA==',false), /closed/);
console.log('ok: native frame helper reuses unchanged hashes, hashes exact edge tiles, verifies full frames and rejects bad input');
