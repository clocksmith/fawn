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
  six unsigned comparisons, empty arms, all-return merges, nested robust Phi
  predecessors, discarded-store-then-return, sequential observations, offset wrap,
  independent resource bounds, empty/unequal buffers, nonzero tails and y/z.
  Every admitted fixture passes external Vulkan 1.1 SPIR-V validation. Deliberate
  guard, subtraction, return and unreachable faults exercise oracle sensitivity.
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
y/z indices, chained/parenthesized arithmetic, logical conditions, else-if,
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
Current developer tests additionally cover the exact 64/65 expression-node,
16/17 expression-depth, 16/17 statement and legacy leading-alias accounting
boundaries. `test_cli_modes.py` now routes both additional unchanged real sources
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
