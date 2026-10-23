# CATSCAN: Experimental C compiler

Parent: [Experimental C migration](../CATSCAN.md)

## Target

Translate the explicitly admitted WGSL surface into independently usable, source-linked target artifacts using only a native C toolchain.

## Authority

- Owns lexical admission, parsing, validation, typed IR, robustness lowering, target emission, reflection and diagnostics.
- Does not own native device execution or application acceptance.

## Scope

- Includes the evolving standalone C compiler; initial coverage is the [first milestone](MILESTONE_1.md).

## Contracts

- Inputs: caller-owned source and explicitly sized output buffers through the versioned C API.
- Outputs: SPIR-V, reflection and deterministic original-source diagnostics.

## Invariants

- Unsupported syntax/types/targets fail explicitly without fallback.
- Failure publishes no partial output and retains no caller pointers.
- Reflection and emitted behavior derive from validated typed IR, not source matching.
- Limits reject rather than truncate; checked arithmetic precedes casts and memory accesses.

## Acceptance

- Frozen milestone tests, independent instruction execution, SPIR-V target validation, sanitizer boundaries and ordinary GCC consumption pass.
- Complete compiler migration additionally requires the retained support matrix, target interfaces and real adopter gates; a milestone pass is not completion.

- Evidence: [compiler tests](tests/) and [first milestone](MILESTONE_1.md).

## Non-goals

- A second host-side interpretation, GPU simulation as qualification or a universal-framework rewrite.

## Freedom

Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
