import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

// Full-buffer alternatives for independently declared float32 references.
// Each element admits only its explicit finite values, never a tolerance band.
export function f32OneOfProblem(expectation) {
  if (Object.keys(expectation).some((key) => !['kind', 'values'].includes(key))) {
    return 'f32BufferOneOfV1 accepts only kind and values';
  }
  if (!Array.isArray(expectation.values) || expectation.values.length === 0) {
    return 'f32BufferOneOfV1 requires a nonempty values array';
  }
  for (const choices of expectation.values) {
    if (!Array.isArray(choices) || choices.length < 1 || choices.length > 2
        || choices.some((value) => typeof value !== 'number' || !Number.isFinite(value)
          || Math.fround(value) !== value)
        || new Set(choices).size !== choices.length) {
      return 'each f32BufferOneOfV1 element needs one or two distinct finite float32 values';
    }
  }
  return '';
}

export function validateF32OneOf(view, expectation) {
  const problem = f32OneOfProblem(expectation);
  if (problem) return { ok: false, detail: problem };
  if (view.byteLength !== expectation.values.length * 4) {
    return { ok: false, detail: 'f32BufferOneOfV1 requires the complete declared byte count' };
  }
  const data = new DataView(view.buffer, view.byteOffset, view.byteLength);
  for (let index = 0; index < expectation.values.length; index += 1) {
    const actual = data.getFloat32(index * 4, true);
    if (!Number.isFinite(actual) || !expectation.values[index].includes(actual)) {
      return { ok: false, detail: `float32[${index}] is outside its explicit CPU reference alternatives` };
    }
  }
  return { ok: true };
}

// A direct plan's command-graph output_oracle (schema 2 exact SHA-256 or
// schema 3 float32 reference tolerance), checked on the bytes the timed run
// read back, with the comparison rule the native runtimes use
// (runtime/c/runtime-cli/src/oracle_compare.c): a non-finite value or
// |actual - expected| > abs + rel * |expected| is a mismatch.
const OUTPUT_ORACLE_KIND = 'outputOracleV1';
const SHA256_HEX = /^[0-9a-f]{64}$/u;
const referenceCache = new Map();

function oracleField(oracle, snake, camel) {
  return oracle[snake] ?? oracle[camel];
}

export function isOutputOracleExpectation(expectation) {
  return expectation?.kind === OUTPUT_ORACLE_KIND;
}

// Converts a plan command's output_oracle into a readback expectation; throws
// for anything the timed command graph cannot observe as declared.
export function outputOracleExpectation(oracle, byteLength) {
  const schemaVersion = oracleField(oracle, 'schema_version', 'schemaVersion');
  if (![2, 3].includes(schemaVersion) || oracle.scope !== 'command_graph'
      || oracleField(oracle, 'dispatch_count', 'dispatchCount') !== 1
      || oracle.initialization !== 'zero_fill_v1'
      || oracleField(oracle, 'reference_class', 'referenceClass') !== 'independent_v1') {
    throw new Error('package plans observe only schema 2/3 independent command_graph oracles over one dispatch');
  }
  const kind = schemaVersion === 3 ? 'float32_reference_tolerance_v1' : 'sha256_exact_v1';
  if (oracle.kind !== kind) throw new Error(`output_oracle schema ${schemaVersion} kind must be ${kind}`);
  const expectedSha256 = String(schemaVersion === 3
    ? oracleField(oracle, 'reference_sha256', 'referenceSha256')
    : oracleField(oracle, 'expected_sha256', 'expectedSha256'));
  const expectation = {
    kind: OUTPUT_ORACLE_KIND,
    oracleKind: kind,
    referenceId: String(oracleField(oracle, 'reference_id', 'referenceId') ?? ''),
    expectedSha256,
    byteLength,
  };
  if (schemaVersion === 3) {
    expectation.referencePath = String(oracleField(oracle, 'reference_path', 'referencePath') ?? '');
    expectation.absoluteTolerance = Math.fround(Number(oracleField(oracle, 'absolute_tolerance', 'absoluteTolerance') ?? 0));
    expectation.relativeTolerance = Math.fround(Number(oracleField(oracle, 'relative_tolerance', 'relativeTolerance') ?? 0));
  }
  const problem = outputOracleProblem(expectation);
  if (problem) throw new Error(problem);
  return expectation;
}

export function outputOracleProblem(expectation) {
  if (!expectation.referenceId) return 'outputOracleV1 requires a referenceId';
  if (!SHA256_HEX.test(expectation.expectedSha256)) return 'outputOracleV1 requires a lowercase SHA-256';
  if (!Number.isInteger(expectation.byteLength) || expectation.byteLength <= 0) {
    return 'outputOracleV1 requires a positive byteLength';
  }
  if (expectation.oracleKind === 'float32_reference_tolerance_v1') {
    const { absoluteTolerance: abs, relativeTolerance: rel } = expectation;
    if (!expectation.referencePath) return 'outputOracleV1 float32 oracle requires a referencePath';
    if (!(abs >= 0) || !(rel >= 0) || (abs === 0 && rel === 0)) {
      return 'outputOracleV1 float32 oracle requires nonnegative, nonzero tolerances';
    }
    if (expectation.byteLength % 4) return 'outputOracleV1 float32 oracle requires a multiple-of-four byteLength';
  } else if (expectation.oracleKind !== 'sha256_exact_v1') {
    return `unknown outputOracleV1 oracleKind ${expectation.oracleKind}`;
  }
  return '';
}

function referenceBytes(expectation, repoRoot) {
  const path = resolve(repoRoot, expectation.referencePath);
  let bytes = referenceCache.get(path);
  if (!bytes) {
    bytes = readFileSync(path);
    referenceCache.set(path, bytes);
  }
  if (createHash('sha256').update(bytes).digest('hex') !== expectation.expectedSha256) {
    throw new Error(`oracle reference ${expectation.referencePath} differs from its frozen SHA-256`);
  }
  return bytes;
}

export function validateOutputOracle(view, expectation, repoRoot) {
  const problem = outputOracleProblem(expectation);
  if (problem) return { ok: false, detail: problem };
  if (view.byteLength !== expectation.byteLength) {
    return { ok: false, detail: `output oracle needs ${expectation.byteLength} bytes, read ${view.byteLength}` };
  }
  const actualSha256 = createHash('sha256').update(view).digest('hex');
  if (expectation.oracleKind === 'sha256_exact_v1') {
    return actualSha256 === expectation.expectedSha256
      ? { ok: true }
      : { ok: false, detail: `output oracle ${expectation.referenceId}: sha256 ${actualSha256} differs` };
  }
  const reference = referenceBytes(expectation, repoRoot);
  if (reference.byteLength !== view.byteLength) {
    return { ok: false, detail: 'output oracle reference length differs from the read-back range' };
  }
  const actual = new DataView(view.buffer, view.byteOffset, view.byteLength);
  const expected = new DataView(reference.buffer, reference.byteOffset, reference.byteLength);
  let mismatchCount = 0;
  for (let offset = 0; offset < view.byteLength; offset += 4) {
    const value = actual.getFloat32(offset, true);
    const want = expected.getFloat32(offset, true);
    if (!Number.isFinite(value) || !Number.isFinite(want)
        || Math.abs(value - want) > expectation.absoluteTolerance + expectation.relativeTolerance * Math.abs(want)) {
      mismatchCount += 1;
    }
  }
  return mismatchCount === 0
    ? { ok: true }
    : { ok: false, detail: `output oracle ${expectation.referenceId}: ${mismatchCount} of ${view.byteLength / 4} floats outside tolerance` };
}
