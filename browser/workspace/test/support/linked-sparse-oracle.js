// Private fixture only. This observer lives in the authorized capture world and returns no image bytes.
import assert from 'node:assert/strict';
import { canonicalRestrictedFrame } from '../../src/adapters/compositor-capture.js';

export function observedReader(real) {
  let previous;
  return async (frame, width, height, tile, prior, copyRgba, canonicalTiles) => {
    const columns = Math.ceil(width / tile);
    const candidates = [];
    const copy = async (source, w, h, rect) => {
      if (source !== frame) throw Error('Unexpected source for canonical evidence');
      if (rect) candidates.push(rect.y / tile * columns + rect.x / tile);
      return copyRgba(source, w, h, rect);
    };
    const result = await real(frame, width, height, tile, prior, copy, canonicalTiles);
    const bytes = await copyRgba(frame, width, height);
    if (bytes.length !== width * height * 4) throw Error('Incomplete full-frame oracle');
    const oracle = [];
    for (let y = 0; y < height; y += tile) {
      for (let x = 0; x < width; x += tile) {
        const rowBytes = 4 * Math.min(tile, width - x);
        const rows = Math.min(tile, height - y);
        const block = new Uint8Array(rowBytes * rows);
        for (let row = 0; row < rows; row++) {
          const offset = ((y + row) * width + x) * 4;
          block.set(bytes.subarray(offset, offset + rowBytes), rowBytes * row);
        }
        const digest = await crypto.subtle.digest('SHA-256', block);
        oracle.push(Array.from(new Uint8Array(digest), v => v.toString(16).padStart(2, '0')).join(''));
      }
    }
    const sameShape = previous?.width === width && previous?.height === height && previous?.tile === tile;
    const changed = sameShape ? oracle.flatMap((h, i) => h === previous.hashes[i] ? [] : [i]) : null;
    if (oracle.length !== result.record.hashes.length || oracle.some((h, i) => result.record.hashes[i] !== h))
      throw Error('Canonical full-frame oracle disagrees with producer tile hashes');
    if (result.usedNativeTiles && (!changed || changed.some(i => !candidates.includes(i))))
      throw Error('Actual dirty candidates omitted canonical changes');
    const audit = { mediaTimestampUs: frame.timestamp, sourceFormat: frame.format, width, height, tile,
      serial: result.serial, previousSerial: prior?.serial ?? 0, candidates, changed, hashes: oracle,
      usedNativeTiles: result.usedNativeTiles, usedNativeEquality: result.usedNativeEquality,
      rejectedTileReads: result.rejectedTileReads ?? 0, producerCopyBytes: result.copiedBytes,
      producerHashBytes: result.hashedBytes, sourcePlaneSnapshotCopyBytes: result.snapshotBytesCopied,
      oracleCopyBytes: bytes.length, oracleHashBytes: bytes.length };
    previous = { width, height, tile, hashes: oracle };
    const observations = globalThis.__blitzenTestSparseRecords ??= [];
    if (observations.length >= 64) throw Error('Observer was not drained');
    observations.push(audit);
    return result;
  };
}

export function attachSparseOracle(compositor) {
  const samples = [];
  const sample = compositor.snapshot.bind(compositor);
  compositor.snapshot = async (workspace, page, send, args, capture, signal) => {
    const batch = [];
    const wrapped = async (method, params) => {
      if (method === 'Runtime.evaluate' && params.expression?.includes('globalThis.__blitzenSurface = (')) {
        const original = '(' + canonicalRestrictedFrame.toString() + ')';
        const target = params.expression.indexOf(original);
        assert.ok(target >= 0 && target === params.expression.lastIndexOf(original), 'single canonical reader');
        const observed = '(' + observedReader.toString() + ')(' + original + ')';
        params = { ...params, expression: params.expression.slice(0, target) + observed + params.expression.slice(target + original.length) };
      }
      const result = await send(method, params);
      if (method === 'Runtime.callFunctionOn' && params.functionDeclaration.includes('const current = globalThis.__blitzenSurface') && !result.exceptionDetails) {
        const collected = await send('Runtime.evaluate', { contextId: params.executionContextId,
          expression: 'globalThis.__blitzenTestSparseRecords?.splice(0)', returnByValue: true });
        assert.ok(!collected.exceptionDetails && collected.result.value?.length === 1, 'exact observation from current capture');
        batch.push(collected.result.value[0]);
      }
      return result;
    };
    const result = await sample(workspace, page, wrapped, args, capture, signal);
    const frame = result.regions.find(region => region.selector === capture.selector)?.frame;
    assert.ok(frame && batch.length === 1 && frame.mediaTimestampUs === batch[0].mediaTimestampUs, 'matched live frame');
    samples.push({ ...batch[0], sessionId: frame.sessionId, grantId: frame.grantId, grantRevision: frame.grantRevision });
    return result;
  };
  return samples;
}

export function qualifySparseReview(samples, authority) {
  const find = frame => {
    const matched = samples.filter(s => s.sessionId === frame.sessionId && s.grantId === frame.grantId
      && s.grantRevision === frame.grantRevision && s.mediaTimestampUs === frame.mediaTimestampUs);
    assert.equal(matched.length, 1, 'unique retained sample');
    const [sample] = matched;
    assert.deepEqual(sample.hashes, frame.tileHashes);
    assert.equal(sample.producerCopyBytes, frame.frameHostCopyBytes);
    assert.equal(sample.producerHashBytes, frame.canonicalHashedBytes);
    return sample;
  };
  const reviewed = find(authority.fresh.reviewed), checked = find(authority.fresh.checked);
  assert.ok(reviewed.usedNativeTiles && reviewed.serial > 0 && reviewed.previousSerial > 0
    && reviewed.serial !== reviewed.previousSerial && reviewed.changed?.length > 0
    && reviewed.candidates.length < reviewed.hashes.length && reviewed.rejectedTileReads === 0);
  assert.ok(reviewed.producerCopyBytes > 0 && reviewed.producerCopyBytes < reviewed.oracleCopyBytes);
  assert.ok(checked.usedNativeEquality && checked.producerCopyBytes === 0 && checked.changed?.length === 0);
  assert.equal(authority.fresh.effectCount, 1);
  return { schema: 'blitzen.same-frame-sparse-correctness/v1', reviewed, checked, verifiedFrames: samples.length,
    oracleCopyBytes: samples.reduce((sum, sample) => sum + sample.oracleCopyBytes, 0),
    oracleHashBytes: samples.reduce((sum, sample) => sum + sample.oracleHashBytes, 0),
    diagnosticOnly: true, maskSource: 'linked-restricted-reader', sameFrameOracle: true };
}
