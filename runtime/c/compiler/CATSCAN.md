# CATSCAN: Experimental C compiler

Parent: [Experimental C migration](../CATSCAN.md)

## Target
Translate admitted WGSL into independently usable, source-linked target artifacts
using a native C toolchain.

## Authority
Own lexical admission, parsing, validation, typed IR, robustness lowering,
emission, reflection and diagnostics; exclude device execution and application acceptance.

## Scope
The standalone compiler; [initial coverage](MILESTONE_1.md) is historical.

## Contracts
Caller-owned source and explicitly sized versioned C API outputs produce SPIR-V,
MSL or HLSL through one typed pipeline. Reflection includes exact entry/binding
metadata, HLSL namespaces/spaces and native requirements; diagnostics retain source locations.

## Invariants
Unsupported syntax, types and targets reject without fallback.
Failure publishes no partial output and retains no caller pointers.
Reflection and behavior derive from validated IR, never source matching.
Limits reject rather than truncate; checked arithmetic precedes narrowing and access.

## Acceptance
Frozen milestone tests, independent instruction execution, target validation,
sanitizer boundaries and ordinary GCC consumption pass. Full migration additionally
requires retained support-matrix, target-interface and real-adopter gates.
[MSL](targets/metal/README.md) and [HLSL](targets/hlsl/README.md) developer controls
never establish independent semantics, SDK/device qualification or production adoption.
- Evidence: [numeric/vector owner](MILESTONE_12_RESULTS.md), [independent gate](independent_tests_m12/PORTABLE_USAGE.md)
and [retained tests](tests/).

## Non-goals
Second host interpreters, GPU simulation as qualification or universal frameworks.

## Freedom
Internal algorithms may vary within these boundaries.
