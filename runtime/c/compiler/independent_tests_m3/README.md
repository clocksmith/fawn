# Independent C compiler milestone 3 acceptance

Source-only evaluator for the frozen M3 optional-reflection CLI increment. This
bundle contains no executables and imports no compiler implementation. `m2/`
contains a byte-identical copy of the self-contained accepted M2 acceptance suite,
including its license and source manifest. The three new `_m3.py` entrypoints
sit beside these modules and use ordinary imports without changing Python import paths. `LIBRARY_SOURCE_IDENTITY.json` freezes
all library source/header-private files against contract commit 9e93fa0.

Use installed GCC/G++ 14.2, Python 3 and the approved existing SPIRV-Tools
validator. GNU native tools, glibc, POSIX Linux file operations and ASan/UBSan are
exercised. No download, install, Zig, GPU or remote access is needed. Exact
library and source/prebuilt binary hashes are required by this frozen campaign;
a different toolchain's resulting binary requires separately explained provenance,
not an unqualified cross-platform portability claim.

From this directory, supply a new output directory that does not exist:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 run_acceptance.py \
  --repo-root /path/to/doe \
  --compiler-root /path/to/doe/runtime/c/compiler \
  --prebuilt-cli /path/to/already-built/doe-compiler-c \
  --prebuilt-library /path/to/accepted/libdoe_compiler.so \
  --validator /path/to/approved/spirv-val \
  --out /path/to/new-evidence-directory
```

The runner first snapshots source and separately copies prebuilt artifacts. Fresh
GCC output goes in a new directory; the copied prebuilt stage never rebuilds its
artifact. Tool versions, tool/source/artifact hashes, commands, flags, outputs,
dependencies and individual failures are retained. Both modes call the actual
unchanged `bench/gates/spirv_val_gate.py` `emit_spirv_for_wgsl` function on both
real residual source copies and the real neighbor-sum source. Output bytes are
compared to explicit reflection and API output, executed with the retained oracle,
and independently validated for Vulkan 1.1. All 164 positives and 240 negatives
also exercise single-output mode with source/no-clobber preservation.

`m2/check_cli_m3.py` covers absent versus explicitly empty reflection, option permutations,
missing/duplicate/unknown options, recognized options misused as values, source
limits, symlink/hardlink/alias/path preservation and no-clobber failure. Each
argument subprocess has its own case working directory so accidental relative
output cannot escape the assertion scope. Flag-looking filenames remain usable
with explicit path spelling. `io_wrap.c` independently instruments write, close
and link calls. Both ordinary and ASan/UBSan CLI builds exercise single-output
failure, two-output second-publication rollback, interrupted/short writes and
existing output/symlink preservation. LeakSanitizer is disabled explicitly.

The unmodified M2 runner is a named retained stage. Its trailing `--reflection`
case is missing a value and must still reject. The old precise real-consumer call
without the option is the M3 lift. No M2 expectation is silently removed or changed.

`observe_gate.py` runs gate main exactly once per invocation. It is deliberately
NOT called by the reusable acceptance runner. Do not rerun it casually: the M3
campaign has one retained observation bound to rejected candidate 1. That process
reported PASS despite 96 emit skips. `m2/audit_sources_m3.py` independently invokes only
the unchanged consumer function on the frozen 98 sources with unique output
paths and explicit Vulkan 1.1 validation, avoiding the gate's basename collision.
The accepted candidate 2 audit is a separate receipt, not another full gate run.

Read FROZEN_CASES.md and the final report for exact scope, discovered first defect
and first harness provenance error. This is Linux CPU/compiler-consumer evidence,
not all-corpus, subgroup, native WebGPU, GPU, backend, application, performance or
full Doe migration acceptance. Production replacements remain zero.

`m2/coverage_ledger_m3.py` creates the source-bound ledger from an existing
unique-source audit and exact API diagnostics. It maps first failures to the
reviewed diagnostic owners, labels later stages as not reached, and never runs
the full gate. Each rejected source retains its path/hash, full diagnostic and
original-source offset context. This is not a complete unsupported-feature census.
