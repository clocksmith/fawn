# Independent M3 black-box case freeze

Frozen 2026-10-23 before inspecting the changed CLI. Contract commit 9e93fa0.
Only CLI reflection omission changes admission. All M2 library semantics, API,
header, source files and shared-library bytes must remain identical.

## Artifact modes and real consumer

Run on fresh GCC source-built CLI/library and separately copied prebuilt CLI,
without rebuilding the latter. Snapshot before running. For unchanged residual
inference source, residual compilation-corpus copy and real neighbor-sum fixture,
call unchanged bench/gates/spirv_val_gate.py emit_spirv_for_wgsl directly. Check
its boolean/message, exact input preservation, only one output, no sidecars or
temporaries, byte identity to explicit-reflection CLI and library output, complete
reflection, Vulkan 1.1 validation and M2 instruction oracle exact vectors.
Check the preserved M2 executable rejects this precise unchanged consumer call.
All 164 admitted and 240 rejected M2 fixtures are retained, including explicit
reflection mode. Also exercise single-output mode on the full frozen corpus.

## Argument cases in both modes

Valid required options in either order, and all six option orders when explicit
reflection is supplied. Fail no arguments, missing input, missing output,
missing values on each present option, unknown option, positional argument,
duplicate input/output/reflection (same or differing values), --help, -- delimiter,
empty input/output; explicitly empty reflection fails and is NOT omission.
Trailing --reflection must fail, unlike an absent --reflection. Return code 2
for malformed/missing/duplicate arguments; file/source failures must be nonzero.
Every failure preserves sources, outputs and symlink objects, and publishes none.

## Source/path/no-clobber cases in both modes

Missing input, input directory, empty input, embedded NUL, 65,537-byte input,
unsupported WGSL, invalid source. Valid 65,536-byte input still succeeds.
Missing output directory, output directory, existing output, output equal input,
output symlink to input, dangling output symlink, output hardlink to source,
valid input through a symlink. Long paths/components reject safely; valid path
with spaces/non-ASCII succeeds. Reflection mode additionally covers missing
reflection directory, reflection directory/existing/dangling symlink/source
symlink, reflection equal input, reflection equal output, and output/reflection
lexically different paths that resolve to the same destination. New output must
be rolled back on second publication failure, preserving old reflection.

## Fault injection and sanitizers

Independent interposer injects write #1, close input #1, close output #2 and link
#1 in single-output mode; explicit-reflection mode injects write #1/#2, close
#1/#2/#3, link #1/#2. Inject short writes and EINTR retry in both modes. Assert
nonzero failure, no temporary or partial outputs and unchanged input. Existing
outputs and symlinks must remain intact under injection. Apply compiler CLI
ASan/UBSan and instrumented linker-wrap helper faults, not just library harness.
Retain two-output second-link rollback. LeakSanitizer is disabled explicitly.

## Retained acceptance and one gate observation

Run unchanged self-contained M2 suite against candidate snapshot, classify any
actual historical omitted-option check individually rather than masking failures.
Keep API/capacity/oracle controls, C/C++, guard boundaries, fuzz/mutated corpus,
export/dependency checks. Bind 98 discovery WGSL sources and 36 prebuilt SPVs,
plus duplicate basenames before one unchanged gate --discover-wgsl invocation.
No --compile route. All TMPDIR/log/report paths stay within evaluator. Keep full
stdout/report/exit and audit independent fresh emissions with unique names and
explicit Vulkan1.1 validation. Gate skips are not accepted sources, prebuilt
validation is not fresh output, and process PASS is not full corpus acceptance.

Post-black-box review is limited to small CLI delta and ownership. No source
edits, historical receipt edits, commits, installs, downloads, Zig, GPU or remote.
