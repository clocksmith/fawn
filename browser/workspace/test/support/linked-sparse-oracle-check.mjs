// Synthetic validation of the auditor. Only the linked browser can exercise the native producer.
import assert from 'node:assert/strict';
import { webcrypto } from 'node:crypto';
import { canonicalRestrictedFrame, canonicalRgbaTiles } from '../../src/adapters/compositor-capture.js';
import { observedReader, qualifySparseReview } from './linked-sparse-oracle.js';

if (!globalThis.crypto) globalThis.crypto = webcrypto;
const shape = [6, 4, 2];
const frames = Array.from({length: 3}, (_, i) => ({format:'I420', timestamp:100+i,
  fawnRestrictedPlaneTiles: () => [[1,0,36],[2,1,36,1],[2,2,0]][i]}));
let row;
const before = new Uint8Array(6*4*4);
const after = before.slice(); after[2*4]=50;
const copy = async (frame, width, height, rect) => {
  const pixels = frame === frames[0] ? before : after;
  if (!rect) return pixels.slice();
  const result = new Uint8Array(rect.width*rect.height*4);
  for(let i=0; i<rect.height; i++) {
    const offset = ((rect.y+i)*width+rect.x)*4;
    result.set(pixels.subarray(offset,offset+rect.width*4),i*rect.width*4);
  }
  return result;
};
const run = observedReader(canonicalRestrictedFrame);
for (const frame of frames) row=await run(frame,...shape,row,copy,canonicalRgbaTiles);
const axis={sessionId:'s',grantId:'g',grantRevision:1};
const samples=globalThis.__blitzenTestSparseRecords.map(v=>({...v,...axis}));
const page=s=>({...axis,mediaTimestampUs:s.mediaTimestampUs,tileHashes:s.hashes,frameHostCopyBytes:s.producerCopyBytes,canonicalHashedBytes:s.producerHashBytes});
const authority={fresh:{reviewed:page(samples[1]),checked:page(samples[2]),effectCount:1}};
const accepted=qualifySparseReview(samples,authority);
assert.deepEqual(accepted.reviewed.changed,[1]);assert.equal(accepted.reviewed.producerCopyBytes,16);
assert.equal(accepted.oracleCopyBytes,288);assert.equal(accepted.oracleHashBytes,288);
assert.throws(()=>qualifySparseReview(samples.slice(2),authority));
assert.throws(()=>qualifySparseReview([...samples,samples[1]],authority));
assert.throws(()=>qualifySparseReview(samples.map(s=>({...s,usedNativeTiles:false})),authority));
const bad=observedReader(async(...args)=>{const result=await canonicalRestrictedFrame(...args);result.record.hashes[0]='bad';return result;});
await assert.rejects(bad(frames[0],...shape,null,copy,canonicalRgbaTiles),/disagrees/);
console.log('Synthetic auditor passed full/sparse/repeat; rejected missing/duplicate frame, fallback and corrupted digest.');
