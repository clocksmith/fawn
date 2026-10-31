# CATSCAN: Experimental C compiler

Parent: [Experimental C migration](../CATSCAN.md)

## Target
Translate admitted WGSL into source-linked artifacts.

## Authority
Own parsing, validation, typed IR, robustness (buffer clamps), emission, reflection and diagnostics;
exclude execution and application acceptance.

## Scope
Compiler; [historical coverage](MILESTONE_1.md).

## Contracts
Versioned caller-owned spans produce SPIR-V, MSL or HLSL through shared semantics.
Reflection carries entry/binding metadata, spaces and native requirements;
diagnostics retain source locations. [Specialization](SPECIALIZATION.md) separates
symbolic modules from selected/whole-module artifacts.
[ABI19](include/doe_compiler_hlsl_profile.h) certifies conservative selected HLSL
profiles in the same transaction; existing APIs/records/workspaces remain unchanged.
Browser/offline edits preserve validated WGSL; bounded Wasm imports no host functions.
The allocating [host companion](host/README.md) owns TSIR/explicit DXC orchestration.

## Invariants
Unsupported syntax/types/targets reject without fallback. Failure publishes nothing
and retains no pointers. Reflection/profiles derive from validated IR and complete
emission, never source matching. Unknown profile features retain the baseline.
Limits reject, never truncate; check arithmetic before narrowing/access.
Const/override/runtime stages stay distinct. Deferred facts are not executable
values/layouts; replaced or unrequired initializers are not evaluated.

## Acceptance
Frozen tests, independent execution, target validation, sanitizers and GCC
consumption pass. Migration additionally requires retained support-matrix,
target-interface and real-adopter gates. [MSL](targets/metal/README.md) and
[HLSL](targets/hlsl/README.md) controls never establish independent semantics,
SDK/device qualification or adoption.
- Evidence: [numeric/vector owner](MILESTONE_12_RESULTS.md),
[independent gate](independent_tests_m12/PORTABLE_USAGE.md), [tests](tests/).

## Non-goals
Second interpreters, simulated qualification or universal frameworks.

## Freedom
Algorithms vary within these boundaries.
