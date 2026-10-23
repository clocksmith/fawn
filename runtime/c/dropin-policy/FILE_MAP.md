# Proposed file map and dependency closure

No repository file below is changed by this planning package. Compiler M11 and
the accepted C instance/future/callback implementations remain untouched. Lead
alone installs/commits a reviewed contract. Runtime implementation waits for the
independent case seal.

## New owner: runtime/c/dropin-policy/

- CATSCAN.md, CONTRACT.md, README.md, STYLE.md, PROVENANCE.md: compact owner,
  semantics, independent build route, restricted C style and source identity.
- include/doe_dropin_policy.h: this package's reviewed header, sole ABI source.
- src/policy.c: singleton binding, lazy publication, ownership and route owner.
- src/internal.h: private state/helpers only if policy.c needs a second source;
  do not create an allocator/framework or one file per recognized symbol.
- tools/generate_config.py: one iterative JSON admission/classification pass,
  immutable tables and source receipt. Shared byte parsing, separate behavior
  and typed-ownership duplicate/type policies. No float128 compatibility layer.
- tools/verify_archive.py: validate supplied archive and dependency receipt at
  native build selection; source/header/JSON/target identity mismatches fail.
- Makefile: explicit GCC C11 PIC static archive; strict g++17 consumer with
  exceptions/RTTI disabled; existing Python/stdlib only. Build output under an
  explicit BUILD directory, preferably /tmp. No installed Zig prerequisite.
- tests/: author C/C++/generator/concurrency/death cases after independent seal.
- integration/abi_probe.c, native_consumer.c, native_policy_probe.zig: layout,
  actual-dlopen consumer, and source-bound test-only native visibility where the
  existing private required-proc/diagnostic seam otherwise cannot be observed.
- independent_tests/: evaluator-owned inputs, oracle, seal and reports. The
  author does not create/overwrite evaluator cases or expectations.

Generated_config.c is a deterministic build output, not an additional hand-kept
policy registry. Its const records/byte arrays are compiled into the archive by
GCC. A C-only consumer supplies static fixture manifest/lane descriptors and can
link/run with the archive without any Zig objects/runtime/compiler. Native
manifest/lane projection is caller-owned data, not hidden archive generation.

## Retained native seam

runtime/zig/src/dropin/dropin_policy_c_adapter.zig (new): sole C-header boundary;
imports the header, creates static projected identity arrays, invokes bind then
the C APIs, converts named codes explicitly, and maps status to diagnostic panic.
It contains no route/default/strict/env/manifest-conflict decision or once state.

runtime/zig/src/dropin/dropin_runtime_config.zig: retain existing implementation
for unselected builds; selected public behavior/owner/route calls go exclusively
to the C adapter. Original JSON parsing/once state must be unreachable in the
selected path. Keep symbolNameSlice's public ABI view behavior; selected routing
uses the same view data/length admission in C, not a pre-normalized empty name.

runtime/zig/src/dropin/dropin_proc_manifest.zig: add a narrow immutable projection
of active entry identities from the existing active groups. Keep actual proc
pointers, typed resolveEntryProc checks and assertUniqueEntryGroups here. Do not
copy literal names to a second C registry. Projection must use the same TIER.

runtime/zig/src/contracts/backend.zig: expose a const view of the existing
LaneSpec/LANE_SPECS names and aliases (or equivalent immutable projection). Do not
alter backend selection, other parseLane consumers or governance source. Native
adapter flattens canonical/alias identity rows, not normalization decisions.

runtime/zig/src/dropin/wgpu_dropin_lib.zig: routeAndRecordForName must obtain the
original owner through the selected runtime facade, removing the direct
manifest-first shortcut there only for selected routing if necessary. Public
GetProcAddress and required-proc route calls use that same facade. Do not modify
loadOptionalProc or loader/cache semantics. Original-owner diagnostics and all
last-error/resolved/fallback observations remain source-matched.

Runtime options/source map/test inventory/charters/migration ledger: document
the experimental owner and add real integration tests through owning tools. No
source deletion, regenerated unrelated inventory churn or weakening failing gates.

## Build selection and stale-input prevention

Proposed option: -Dexperimental-c-dropin-policy-archive=/absolute/archive.a.
Absent means retained Zig, with no generator/archive prerequisite. Present means
one independently GCC-built owner, no fallback to Zig on a bad/missing archive.
The option does NOT require C futures/callbacks/instance and must not be nested
inside their build branch. Add the boolean consistently to default, compute and
full build-option modules, plus every test/module consuming those options.
Reject selected unsupported target/ABI rather than accidentally linking a host
archive into another target. No default-provider/backend switch is authorized.

The Make dependency DAG includes both JSON files, both schemas, generator source,
header, all C implementation/private headers, compiler target/options and generated
output. Receipt binds all of them plus exact archive SHA-256, generator admission
profile, compiler version and command. JSON changes regenerate the tables; even
schema-only changes refresh the receipt/archive provenance without silently making
the schema an admission parser. Failed generation uses an atomic replacement
discipline and never appears as successful reuse of stale output.

Native selection verifies receipt and actual archive bytes against current owner
sources/JSON/header/target/options before linking. The native build separately
depends on the adapter, active manifest source/TIER, backend lane source, build
options and pinned WebGPU ABI header. Reading hashes only in a narrative receipt
is insufficient: actual selected build dependency edges and verifier execution
must be demonstrated by incremental edited-input tests. An externally supplied
stale archive either rebuilds through its named Make route or fails selected
configuration with the exact mismatched input; it never silently qualifies.

Current source modules requiring consistent include/link policy: doe_module,
compute_doe_module, full_doe_module and every executable/test root using their
build_options. The exact build.zig closure includes dropin_lib, core_dropin_lib,
full_dropin_lib; exe, module_runner; metal_staged_write_bench, metal_compute_bench;
emit_msl_exe, emit_ir_digest_exe, emit_csl_exe, emit_tsir_attention_canary_exe,
emit_hlsl_exe, emit_dxil_exe, emit_spirv_exe; doe_plan_executor,
webgpu_plan_executor, csl_sim_runner, csl_bundle_emitter, csl_host_plan_tool,
tsir_bootstrap_manifest_inputs, tsir_bootstrap_oracle;
test_exec, device_test_exec, core_test_exec, full_test_exec, d3d12_test_exec,
wgsl_test_exec; the two Metal bench test roots; shader_bench_exe/tests,
host_hotpath_bench_exe/tests, compilation_bench_exe/tests,
runtime_compile_report_exe/tests; and selected instance/callback/futures
integration roots. Add the new route integration root too. Pure C ORT roots and
isolated WGSL/browser roots without this build_options/import path are not new
policy consumers. webgpu_plan_executor, csl_sim_runner and tsir_bootstrap_oracle
import the same selected doe_module even though their roots do not import
build_options directly (build.zig:1200-1256 and1320-1330); include that transitive
closure. This is a bounded include/link configuration, not a new build framework.
Ordinary static linking may discard unused owner code, but no importer may have
unresolved selected-owner calls or a second state authority.

Check link maps/hidden symbol visibility and static descriptor identities for
exactly one route state owner per artifact, including duplicate module imports.
No artifact may read selected=true while omitting its archive, or route partly
through the retained Zig config once state. Source/architecture/inventory gates
and actual consumers, not symbol presence alone, prove closure.
