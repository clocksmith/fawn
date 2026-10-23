# Accepted M11: workgroup storage, initialization and uniformity

The [frozen contract](MILESTONE_11.md) passes independent acceptance on candidate2.
One compiler now owns typed fixed workgroup roots, cooperative initialization,
`workgroupBarrier` and source-level transitive control/data uniformity. The four
public headers and seven exports are unchanged. This is standalone compiler and
CPU target-model evidence, not native runtime adoption or physical GPU execution.

## Decisive evidence

- All 120 effective independently sealed/supplemented cases pass a fresh GCC
  source build, including exact failures and output atomicity in six output modes.
  All emitted cases pass default Vulkan 1.1 validation.
- Independent uniformity expectations include 93 explicit graph slices and
  constant-skipped RHS dependencies. Actual emitted SPIR-V runs under four
  interleaving schedules with poisoned, reused workgroup memory, dynamic barrier
  rendezvous and group isolation. The matrix covers 176 successful dispatches
  and 16 expected race detections. All 17 target mutation controls are detected.
- The unchanged 100-iteration, 200-source-barrier witness passes eight dispatches
  over two isolated groups. Each dispatch executes 435,226,078 target instructions
  and 201 barriers per group, including initialization. The complete case takes
  5,084.373908 seconds. No restart, shortened loop/schedule, or validation-only
  substitute was used. This is test cost, not compiled-code performance.
- The unchanged CLI consumer freshly **emits and validates 52 of 98 paths; 43
  paths are independently executed and nine are validation-only**. The other 46
  paths reject explicitly. All 37 retained M10 target streams are byte-identical
  and re-executed; the six mandatory new witnesses supply the additional execution
  evidence. The [per-source ledger](../../../docs/c-migration/corpus-coverage-m11.json)
  preserves these distinctions and does not count unrelated prebuilt modules.
- Native C/C++ source/archive/shared/prebuilt consumption, ABI queries, exact and
  one-short capacities, workspace reuse, concurrency, allocation self-controls,
  errno/fenv restoration, injected faults and ASan/UBSan pass within the tested
  GCC 14.2/Linux x86-64 envelope. No Zig builds or runs the C compiler.

## Ownership, cost and preserved corrections

Workgroup roots use shared typed access/layout, not fake resource bindings.
Lane zero writes whole typed null roots before the initialization barrier and
before user control flow. Barriers order workgroup memory only, never storage
buffers or other groups. Uniformity analyzes validated source, including syntax
removed by constant short-circuit lowering, with bounded phase-owned storage and
work accounting. Skipped numeric evaluation is not skipped semantic analysis.

Fixed workspace is 146,256 bytes; typed CLI workspace is 2,536,128 and old-job
CLI workspace is 2,172,992. All 23 private records and phase slices match the
frozen ledger. The independent 145-frame/14-unit audit derives conservative
combined bounds of 204,608 fixed and 193,376 maximum-depth job bytes, within
212,992/196,608 ceilings. These are derived library bounds, not observed stack
peaks; system, external library, caller and sanitizer frames are excluded.

The author preserved an earlier unsupported-builtin diagnostic correction before
candidate2. Independent evaluation observed no candidate2 compiler defect.
Original evaluator failures and corrections remain: reserved identifiers,
status-label and constant-index fixture repairs before observation; one exact
private-global PARSE_ERROR-to-UNSUPPORTED historical mapping required by the frozen
contract; and four native supplement failures repaired only in alignment and
REFLECTION-only descriptor expectations. All 576 retained M10 semantic cases
are accounted for. No malformed/type-invalid family was broadly relaxed.

The long runner had no wall-clock timeout or live intra-case counters. Its
recorded estimate was never a deadline or proof of progress. Per-dispatch event
and per-invocation instruction bounds remained fail-closed; final counters and
exit0, not elapsed time, establish completion. Original supervision records remain.

## Reproduction and limits

Use the [portable instructions](independent_tests_m11/PORTABLE_USAGE.md),
[independent report](independent_tests_m11/REPORT.md), structured
[verdict](independent_tests_m11/VERDICT.json), and its separate native replay.
Candidate source manifest SHA256 is
`b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af`;
frozen contract manifest SHA256 is
`9f5013276a311c610cade91062426b9ab6d7f993293c7fcd22ff183902c81f75`.

Other barriers, atomics, pointers, matrices, aliases, extensions, wider numeric
builtins, multiple entries, graphics, textures, other targets and production
TranslationInfo/cache/workgroup-metadata adoption remain open. Nine corpus
emissions lack execution qualification. No GPU, runtime-wide lifetime, performance,
TSan or whole-Doe completion claim follows. No production-default Zig owner is
replaced by this acceptance.
