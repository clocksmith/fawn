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
