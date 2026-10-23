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
- Outputs: SPIR-V, ABI10 experimental MSL and additive ABI11 HLSL artifacts from
  one shared typed pipeline, reflection, target binding/entry metadata and
  original-source diagnostics. HLSL records include register namespaces/spaces,
  exact entry names and dispatch/native feature requirements.

## Invariants

- Unsupported syntax/types/targets fail explicitly without fallback.
- Failure publishes no partial output and retains no caller pointers.
- Reflection and emitted behavior derive from validated typed IR, not source matching.
- Limits reject rather than truncate; checked arithmetic precedes casts and memory accesses.

## Acceptance

- Frozen milestone tests, independent instruction execution, SPIR-V target validation, sanitizer boundaries and ordinary GCC consumption pass.
- Complete compiler migration additionally requires the retained support matrix, target interfaces and real adopter gates; a milestone pass is not completion.

- Experimental MSL has [developer checks](targets/metal/README.md), not Metal SDK or GPU qualification.
- Public HLSL has [typed developer consumption and bounded DXC checks](targets/hlsl/README.md),
  not independent semantic, Windows SDK, device or production-provider qualification.
- Evidence: [accepted numeric/vector owner](MILESTONE_12_RESULTS.md), its
  [independent source-only gate](independent_tests_m12/PORTABLE_USAGE.md), and
  retained [compiler tests](tests/).

## Non-goals

- A second host-side interpretation, GPU simulation as qualification or a universal-framework rewrite.

## Freedom

Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
