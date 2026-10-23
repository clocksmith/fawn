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

These are intentional grammar changes, not weakened validation. Float literals,
wrong scalar types, writes to read-only storage, undefined or shadowed names,
y/z indices, chained/parenthesized arithmetic, logical conditions, else-if,
unsupported blocks/locals and invalid unreachable statements still reject.
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
