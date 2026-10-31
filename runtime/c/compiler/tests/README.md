# Experimental C compiler developer checks

Run `make test SPIRV_VAL=/path/to/already-installed/spirv-val` and `make sanitize`
from the compiler directory. These commands neither install tools nor invoke Zig.
They are developer checks, not the separate independent acceptance or GPU gates.

- `test_api.c`, `test_boundaries.c`, `test_constants.c`: retained milestone-1
  positives and syntax/type/access/scope failures, ABI admission, source/token/name
  limits, original offsets, capacity atomicity, deterministic and concurrent calls.
- `test_control_api.c`: milestone-2 source/output guard pages and canaries,
  maximum-value statements, exact 16/17 statement and 4/5 nesting boundaries,
  unreachable diagnostics, byte mutations and 1,024 concurrent structured calls.
- `test_cli.py`: CLI no-clobber/failure behavior, two unchanged residual shaders,
  retained u32 cases and typed emitted-instruction execution. Its instruction
  interpreter is developer evidence, never a production host fallback.
- `test_cli_modes.py`: unchanged real emitter-consumer calls, optional reflection,
  byte equality, malformed options, missing/unsupported sources and destination
  preservation in both modes. `cli_io_faults.c` supplies test-only linker wrappers
  for partial/failed/interrupted writes, close/temporary/link failures and
  competing file/symlink publication. No wrapper enters shipped artifacts.
- `test_control.py`: unchanged neighbor shader plus renaming/declaration order,
  six unsigned comparisons, empty arms, all-return merges, nested Phi
  predecessors, clamped-store-then-return, sequential observations, offset wrap,
  independent per-resource clamps, empty/unequal buffers, nonzero tails and y/z.
  The interpreter asserts every element pointer of a non-empty binding is in range;
  an empty binding (below the reflected minimum, rejected at dispatch) reads zero
  and drops stores, as Vulkan robustBufferAccess does for an empty range.
  Every admitted fixture passes external Vulkan 1.1 SPIR-V validation. Deliberate
  clamp, subtraction, return and unreachable faults exercise oracle sensitivity.
- Native C/C++ consumers, ABI layout and export/interposition checks retain their
  ordinary standalone-library boundary.

## Explicit milestone-1 rejection lifts

`test_control.py:REJECTION_LIFTS` names formerly rejected programs now admitted by
frozen milestone 2: single load/literal RHS, constant-only addition, literal index,
index addition/subtraction, return-only, early return, sequential assignments,
empty if and if/else. The implementation run replays all eleven through the
preserved milestone-1 executable and records its rejecting diagnostics. The
named constant is also newly valid as an index or index offset/condition operand.

These are intentional grammar changes, not weakened validation. Under current M4, decimal floats and scoped scalar locals are explicitly
admitted. Wrong scalar types, writes to read-only storage, undefined or shadowed names,
logical conditions, else-if,
unsupported bare blocks/mutable locals and invalid unreachable statements still reject.
Two-character comparisons must be contiguous tokens; whitespace or comments
cannot turn separate punctuation into a composite operator.

Sanitizer runs disable LeakSanitizer because it is unavailable in the execution
environment. ASan/UBSan, CPU instruction execution and target validation do not
prove physical GPU behavior, uniformity, barriers, WebGPU conformance, application
adoption or performance advantage.

## Explicit milestone-2 CLI admission lift

The preserved M2 executable rejects the unchanged consumer's complete
`--shader-path INPUT --out OUTPUT` invocation with usage status 2 because
reflection was required. Milestone 3 admits that invocation; it is the only
new CLI mode. Explicit `--reflection` without a value, duplicate options,
unknown options and missing required options remain errors. An explicit empty
reflection path fails rather than silently selecting binary-only mode. A recognized
option token cannot supply a missing path. Tests preserve ordinary leading-dash
filenames and exercise `./--out`, `./--reflection` and `./--shader-path` as
unambiguous actual path names.
Historical independent CLI tests are not edited or reclassified as passes.

Run `make test-cli-modes SPIRV_VAL=/path/to/spirv-val` for the focused test and
`make sanitize-cli SPIRV_VAL=/path/to/spirv-val` to instrument both the actual
CLI and its fault-injection build with ASan/UBSan. Both invoke the tests as a
module from the repository root, without changing Python import paths. The
validator and optional `DOE_COMPILER_CLI` / `DOE_COMPILER_FAULT_CLI` paths must
refer to existing host artifacts. These checks do not run the full discovery
gate or establish source-corpus, runtime, application or GPU qualification.

## Milestone-4 scalar expression coverage

`test_expressions.py` extends the developer instruction oracle with integer and
float multiplication, float subtraction, ordered float comparison and selection.
It checks unchanged scale/ReLU sources and renamed/declaration-order/comment
variants, precedence/grouping/associativity, u32 wrap, float operation rounding,
local snapshots after writes, branch scope/dominance/returns, robust unequal
lengths, builtin/type shadow lookup and excluded constant-only f32 subtrees.
Its independent positive-encoding binary search compares exact rational
distances for 164 bounded decimals, including ties, neighbors and the smallest
admitted nonzero decimal. Deliberate new-opcode faults and unknown-opcode
rejection test the developer oracle, not just the compiler.

`test_expression_api.c` adds source/output guard pages, every short output
capacity, atomic rejection, all source prefixes, deterministic byte mutations,
1,024 concurrent expression compiles and four rounding modes. It compares
outputs under installed C/C.utf8/German/French numeric locales without changing
those settings in the compiler. Missing test locales are printed explicitly.
Both ordinary and ASan/UBSan builds run from the Makefile.

Three old developer expectation sites are intentionally lifted by the frozen
M4 contract, rather than silently skipped:

- `test_api.c` formerly rejected the seed's `values[id.x] - 1u`. It now asserts
  admission; `test_expressions.py` independently executes subtraction results,
  including associativity and wrap. Division replaces the unsupported-operator
  negative, which remains outside M4.
- `test_control.py` formerly rejected `out[0u]=src[0u]-1u;`. The exact fixture now
  executes input three to output two in `M4_REJECTION_LIFTS`.
- It formerly rejected `if(gid.x==0u){let q=gid.x;}`. The exact fixture now
  executes without changing storage, and a companion fixture uses that local
  in branch-dependent multiplication. Mutable `var` remains rejected.

Historical independent M1/M2/M3 suites and their earlier verdicts are unchanged.
M4 developer tests originally covered 64/65 expression-node and 16/17
expression-depth limits. M6 retains those admitted shapes and moves the unified
limits to 320/321 nodes and 18/19 depth, counting all index edges. The 16/17
statement and legacy leading-alias boundaries remain unchanged. `test_cli_modes.py` now routes both additional unchanged real sources
through the same existing consumer and verifies binary/reflection equality and
explicit Vulkan 1.1 target validation. No full discovery gate or GPU is run.

## Milestone-5 uniform/workspace coverage

`make test-uniform` compiles actual native C/C++ consumers and checks v2 field
layout, caller malloc storage, NULL/short/misaligned/oversized workspace, version
and exact-size no-write preflight, reuse after failures, guard pages, every short
output capacity, source prefixes/mutations and 512 parallel compiles. The prebuilt
C consumer reads unchanged `package_vector_scale_add.wgsl`. Export tests require
exactly three ABI functions and no imported allocator symbols.

`test_uniforms.py` exercises emitted Uniform storage-class pointers and member
Offset decorations using independently packed raw byte buffers, not reflected
values substituted into formulas. It covers the unchanged witness, count bounds,
independent unequal storage lengths, zero/short inputs, preserved outputs/tails,
y/z independence, all members in 1/3/4/5/8-field layouts, shared types/resources,
mixed scalars, snapshots, member indices/comparisons and wrong-offset mutation
controls. It rejects short uniform buffers before invocation. Syntax/type/scope,
dead-source and exact struct/member/resource limit tests remain fail-closed.
These extend only the developer oracle; historical independent suites stay intact.

CLI tests run existing ownership/I/O/race/rollback cases in binary-only, legacy
and v2 reflection modes, plus exact allocation counts/failures and cleanup.
The query-failure wrapper asserts no allocation follows a failed query; allocation
wrappers assert all successful acquisitions were freed. Binary-only uses exactly
workspace/result allocations and no JSON allocation. Sanitizer CLI tests run the
same checks through instrumented real CLI sources. Legacy reflection still
rejects every struct/uniform module, including unused declarations; malformed or
type-invalid cases were not relabeled as intentional lifts. M5 therefore needs
no change to retained ABI1 negative expectations.

## Milestone-6 unified scalar and compute-input coverage

`make test-compute` runs `test_compute_api.c` and `test_compute.py`. The native
check uses all five reordered builtin inputs, nested storage indices/conditions,
a zero-input no-op, ABI1/v2 target equivalence, source/workspace guard pages,
every short output capacity, atomic errors, source prefixes and byte mutations,
reuse across successes/failures/no-ops, and 1,024 parallel calls. Its ASan/UBSan
route is `make sanitize-compute`; retained native/CLI sanitizers remain separate.

The opcode tests compile all six unchanged M6 witnesses and validate Vulkan 1.1.
Gather uses raw packed uniforms and unequal/empty token/table resources, retained
outputs, UINT32_MAX tokens and coherent 256-wide global dispatch. Copy uses actual
WorkgroupId input identity. No-op tests assert zero storage effects and only
declared inputs. A race-free 3D dispatch exercises every builtin component,
reordered parameters, scalar local index, 63-byte entry names and workgroup
LocalSize reflection. Decoration/component/clamp-select mutation controls check
oracle sensitivity. Synthetic uint32 injections separately exercise wrap and clamped
nested reads/stores. Nested load-valued comparisons, snapshots, scope/type errors,
320/321-node and 18/19-depth limits use the actual parser and emitter.

The precise retained negative lifts are intentional, not blanket exclusions:

- `test_api.c`: workgroup_size(1,1,1), local_invocation_id and destination id.y
  become explicit positives. Four dimensions, subgroup_invocation_id and id.w
  remain nearby negatives; all old malformed/type/access negatives remain.
- `test_control.py`: empty body, gid.y index, parenthesized gid.x index,
  gid.x+1u+2u, gid.x+gid.x, and gid.x==gid.x are now executed positives in
  `test_compute.py:test_precise_retained_negative_lifts`.
- `test_expressions.py`: let-valued index offsets and comparison RHS become
  executed positives in the same M6 method. Previous 17-level/65-node rejections
  are explicitly admitted alongside their old accepted neighboring cases;
  new exact limits have independent positive/negative fixtures.
- `test_uniforms.py`: p.x+p.z index, p.z+1u comparison RHS and global-x comparison
  RHS become executed positives with packed uniforms in the M6 method.

No old invalid scalar type, scope/access violation, malformed operator, dead-source
error or unsupported grammar is reclassified. The historical independent suites
and verdicts are unchanged; cross-version target byte identity is not required,
but current ABI/CLI mode equivalence, semantics and metadata remain mandatory.

## M7 job/state/control owner

`make test-job SPIRV_VAL=/path/to/already-installed/spirv-val` runs the native job
API capacity/atomicity/mutation/reuse/concurrency suite, shared-library C++17 job
consumer, and emitted-opcode/explicit Vulkan1.1 checks in `test_job.py`.
`job_machine.py` extends the existing developer opcode oracle with invocation-local
Function memory, loop metadata, bool constants/operators and ordered/unordered
float comparisons. Its step limit reports unfinished execution and is not shader
semantics. Five unchanged real loop witnesses execute with separately packed
uniforms and storage, including zero/short input observations.

`test_job_api.c` additionally preserves exact ABI1/v2 320/321 scalar-node,
16/17-statement, exempt-alias, 16 uncharged comparison-wrapper and 18/19-depth
boundaries. It checks the configured job maximum depth independently of storage,
source >65536, staged/caller word failure, invalid/short/misaligned workspace,
version failure, and deterministic parallel workspace reuse. `make sanitize-job`
instruments this same source; library allocation is absent.

Current developer CLI-v2 suites explicitly lift the historical M6 logical-OR,
module/nested local-shadowing, 17-statement, 321-node and depth19 rejections under
M7's job profile. Their exact sources now have positive target/oracle checks,
not permissive accept-or-reject assertions. Malformed/type-invalid cases remain
negative. `test_exports.py` names exactly five public ABI functions. CLI fault
coverage injects every allocation in the new 4/3/5 binary/legacy/v2 ownership
paths and retains existing publication-race/no-clobber behavior.

## ABI9 software Vulkan atomics consumer

`test_atomic_vulkan.c` compiles real WGSL through the ABI9 C library, then uses
an explicitly supplied C Vulkan backend archive. `atomic_operations.wgsl` checks
all eleven i32/u32 operations in storage and workgroup address spaces, negative
and upper-bound OOB accesses, compare-exchange result fields, zero initialization,
and 344 exact readback words including final cells. Result storage begins with a
nonzero sentinel. A second dispatch consumes unchanged
`bench/kernels/workgroup_atomic.wgsl` and compares all 256 words with an independent
finite integer oracle. Both check readback padding and early release/pending guards.

The compiler Makefile owns both software consumers and asks the Vulkan owner
to build its archive. From the repository root:

```sh
make -C runtime/c/compiler test-cpu-vulkan \
  SOFTWARE_VULKAN_ICD=/path/to/software-icd.json
```

The consumer requires a software adapter and deliberately does not run the
retained 100-iteration variant. This is bounded software execution evidence,
not physical GPU qualification, WebGPU conformance or performance parity.
`test-atomic-vulkan` and `test-pointer-vulkan` run either client separately.
Both require an existing explicitly selected software ICD; neither is part of
ordinary host checks. `CONSUMER_BUILD`, `VULKAN_BUILD` and `SHARED_LIBRARY`
select their test outputs, backend build and compiler artifact.

`make test-consumers` runs the ordinary shared-library `test_atomic_cpp.cpp`
ABI9 measurement and `test_phony_api.c` entry-closure/publication controls without
Vulkan. `test-atomics` includes the C++ consumer, and `test-text` includes the
phony-assignment API consumer. Either can run alone with `test-atomic-cpp` or
`test-phony`, including against an explicitly selected Darwin compiler dylib.
