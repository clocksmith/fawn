import assert from 'node:assert/strict';
import '../oracles/particle_step.js';

const before = new Float32Array(256 * 8);
const after = new Float32Array(before.length);
for (let p = 0; p < 256; p++) {
  const i = p * 8;
  before.set([0.2, 0.1, 0.1, 0, 0.2, 0.3, 0.4, 0.002], i);
  after.set([0.2, 0.1, 0.0986, 0, 0.2, 0.3, 0.4, 0.0016 + 0.0986 * 0.0018], i);
}
const uniforms = [0, 0, 1, 0, 0, 0, 1, 0];
assert.equal(globalThis.__particleCpuOracle(before, after, uniforms, 1, 65536).matched, true);
after[255 * 8 + 3] = 0.1;
const wrong = globalThis.__particleCpuOracle(before, after, uniforms, 1, 65536);
assert.equal(wrong.matched, false);
assert.equal(wrong.particle, 65535);
assert.throws(() => globalThis.__particleCpuOracle(before, after, uniforms, 0, 65536));
console.log('particle scalar oracle controls passed');
