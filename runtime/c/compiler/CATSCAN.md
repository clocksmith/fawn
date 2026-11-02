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
Versioned caller-owned spans produce SPIR-V, MSL or HLSL through shared semantics.
Reflection carries bindings/native requirements; diagnostics retain locations.
[Specialization](SPECIALIZATION.md) separates
symbolic modules from selected/whole-module artifacts.
[ABI19](include/doe_compiler_hlsl_profile.h) certifies selected HLSL profiles transactionally.
Browser/offline preserves validated WGSL; Wasm imports no host functions.
[Prepared transactions](include/doe_compiler_prepared.h) retain one analysis through
exact-size publication in caller-owned workspace; ABI1-19 layouts/semantics remain unchanged.
[Host companion](host/README.md) owns allocation and TSIR/DXC orchestration.

## Invariants
Unsupported syntax/types/targets reject without fallback. Failure publishes nothing.
Synchronous calls retain no pointers; prepared transactions borrow stable spans
within their explicit lifetime. Reflection/profiles derive from validated IR and
complete emission; unknown profile features retain baseline.
Limits reject without truncation; check arithmetic before narrowing/access.
Const/override/runtime stages stay distinct; deferred facts are not executable.
Replaced/unrequired initializers are not evaluated.

## Acceptance
Frozen tests, independent execution, target validation, sanitizers and GCC
consumption pass. Migration requires support-matrix,
target-interface and real-adopter gates. [MSL](targets/metal/README.md) and
[HLSL](targets/hlsl/README.md) controls never establish independent semantics,
SDK/device qualification or adoption.
- Evidence: [numeric/vector owner](MILESTONE_12_RESULTS.md),
[independent gate](independent_tests_m12/PORTABLE_USAGE.md), [tests](tests/).

## Non-goals
Second interpreters, simulated qualification or universal frameworks.

## Freedom
Algorithms vary within boundaries.
