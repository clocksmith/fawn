# CATSCAN: Experimental C compiler

Parent: [Experimental C migration](../CATSCAN.md)

## Target
Translate admitted WGSL into source-linked artifacts with C.

## Authority
Own lexical admission, parsing, validation, typed IR, robustness lowering,
emission, reflection and diagnostics; exclude device execution and application acceptance.

## Scope
The compiler; [initial coverage](MILESTONE_1.md) is historical.

## Contracts
Caller-owned spans and versioned APIs produce SPIR-V,
MSL or HLSL through one typed pipeline. Reflection includes entry/binding
metadata, HLSL spaces and native requirements; diagnostics retain source locations.
The browser/offline adapter preserves validated WGSL with typed source edits;
its bounded Wasm reactor imports no host functions.
[ABI13 specialization](SPECIALIZATION.md) separates symbolic module validation
from concrete selected-entry emission through the same semantic owner.

## Invariants
Unsupported syntax, types and targets reject without fallback.
Failure publishes nothing and retains no pointers.
Reflection and behavior derive from validated IR, never source matching.
Limits reject rather than truncate; checked arithmetic precedes narrowing and access.
Const, override and runtime stages remain distinct. Deferred facts are not
executable values/layouts; replaced or unrequired initializers are not evaluated.

## Acceptance
Frozen tests, independent execution, target validation,
sanitizer boundaries and GCC consumption pass. Full migration
requires retained support-matrix, target-interface and real-adopter gates.
[MSL](targets/metal/README.md) and [HLSL](targets/hlsl/README.md) developer controls
never establish independent semantics, SDK/device qualification or production adoption.
- Evidence: [numeric/vector owner](MILESTONE_12_RESULTS.md), [independent gate](independent_tests_m12/PORTABLE_USAGE.md)
and [retained tests](tests/).

## Non-goals
Second host interpreters, GPU simulation as qualification or universal frameworks.

## Freedom
Internal algorithms may vary within these boundaries.
