# CATSCAN: C compiler

Parent: [C runtime](../CATSCAN.md)

## Target
Translate admitted WGSL into source-linked artifacts.

## Authority
Own parsing, validation, typed IR, buffer robustness, emission, reflection and diagnostics;
exclude execution and application acceptance.

## Scope
Compiler and [historical coverage](MILESTONE_1.md).

## Contracts
Versioned caller spans emit SPIR-V, MSL or HLSL via shared semantics.
Reflection carries bindings/requirements; diagnostics retain locations.
[Specialization](SPECIALIZATION.md) separates symbolic modules from artifacts.
[ABI19](include/doe_compiler_hlsl_profile.h) certifies HLSL profiles transactionally.
Browser/offline preserves validated WGSL; Wasm imports no host functions.
[Prepared transactions](include/doe_compiler_prepared.h) retain one analysis in
caller workspace without layout growth; ABI18/20 retain pre-fatal notices (ABI1-17/19 unchanged).
[Host companion](host/README.md) owns allocation and TSIR/DXC orchestration.

## Invariants
Unsupported syntax/types/targets reject without fallback. Failure publishes no code or metadata; ABI18/ABI20 retain pre-fatal source notices.
Synchronous calls retain no pointers; prepared transactions borrow spans within
explicit lifetimes. Reflection/profiles derive from validated IR and complete
emission; unknown features retain baseline.
Limits reject without truncation; check arithmetic before narrowing/access.
Const/override/runtime stages stay distinct; deferred facts stay non-executable.
Replaced/unrequired initializers stay unevaluated.

## Acceptance
Frozen tests, independent execution, target validation, sanitizers and GCC pass. Migration requires support-matrix,
target-interface and adopter gates. [MSL](targets/metal/README.md) and
[HLSL](targets/hlsl/README.md) controls never establish independent semantics,
device qualification or adoption.
- Evidence: [numeric/vector owner](MILESTONE_12_RESULTS.md),
[independent gate](independent_tests_m12/PORTABLE_USAGE.md), [tests](tests/).

## Non-goals
Second interpreters, simulated qualification or universal frameworks.

## Freedom
Algorithms vary inside boundaries.
