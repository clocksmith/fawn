# DXIL C port: test results

Run on this Linux VM (Ubuntu, GCC 13.3.0, Python 3.12.3) on 2026-10-26.
Source under test: `~/workspace/doe-c-work/dxil/` (this directory).
Read-only Zig source: `~/workspace/fawn-tree`, head
`43040741d1d4822d64c8ec8976d6c652ec345242`. Nothing in that tree was
modified, and nothing was pushed to GitHub.

## Commands and results

All commands were run from `~/workspace/doe-c-work/dxil`.

### 1. Plain build and test

Command:

```sh
make check
```

Result: PASS. All ten test binaries passed, 955 checks total, zero
failures:

| Test binary | Checks | Result |
| --- | --- | --- |
| `test_dxil_md5` | 7 | PASS |
| `test_dxil_bitcode` | 64 | PASS |
| `test_dxil_container` | 55 | PASS |
| `test_dxil_validate` | 53 | PASS |
| `test_dxil_builder_serialize` | 336 | PASS |
| `test_dxil_mapping_toolchain` | 230 | PASS |
| `test_dxil_ir_native` | 123 | PASS |
| `test_dxil_hlsl` | 13 | PASS |
| `test_dxil_emit` | 29 | PASS |
| `test_dxil_analyze_translate` | 45 | PASS |

Coverage notes for the four new binaries: `test_dxil_ir_native`
covers type interning/dedup (including distinct structs never equal),
float/signed predicates, std430 layout (field offsets 0 and 16,
struct size and minimum binding size 32), ir_query helpers (int
literal matching, builtin component classification), native emit of
the canonical compute module (container over 32 bytes starting
`DXBC`, validating with kind 5, 4 parts, and a test-side bitcode
scanner asserting exactly 1 function block with 3 CALL ops and a
trailing RET plus metadata payloads for `dx.version`, `dx.valver`,
`dx.shaderModel`, `dx.entryPoints`, and `cs`), a barrier body with
exactly 1 CALL op, no-entry-point and out-of-range-entry
`InvalidIr`, placeholder semantics (assign with an `address_of` lhs
succeeds, with an `address_of` rhs fails `UnsupportedConstruct`),
and 64 bound resources succeeding while the 65th fails
`OutputTooLarge` (unbound and unclassifiable globals skipped).
`test_dxil_hlsl` asserts exact HLSL text for the canonical compute
module and for a storage-buffer/barrier/arithmetic module, the
dispatch cbuffer shape for a `num_workgroups` parameter, and
`UnsupportedBuiltin` for an atomic builtin plus
`UnsupportedConstruct` for a vertex entry. `test_dxil_emit` covers
`dxil_lower` stage/name/workgroup/model mapping and no-entry-point
`InvalidIr`, primary emit success and too-small-buffer
`OutputTooLarge`, a cleared diagnostic on native-emitter failure, and
diagnostic unit checks (message formats, source-context snippet at
2:4, kind mapping). `test_dxil_analyze_translate` covers the
canonical WGSL source end to end (kind COMPUTE, 3 CALL ops, trailing
RET), a storage/barrier/arithmetic source, a vertex source (kind
VERTEX), `UnknownIdentifier` / `UnexpectedToken` / `UnknownType` /
`DuplicateSymbol` failures, empty-source success, no-entry-point
`dxil_emit: InvalidIr` mapping, and the toolchain translate path
against a fake DXC script that copies a pre-made valid container to
the `-Fo` path (byte-identical output), plus empty and missing
executables yielding `ShaderToolchainUnavailable`.

Coverage notes: the bitcode test asserts exact VBR chunk bits, signed
rotation values, block length-word patching (word == 1, size == 12 for
the canonical case), depth-0 exit and width-1 VBR errors, and the
16-deep nesting cap. The container test asserts the frozen
empty-container MD5 oracle (`339ca4aa7ccd6132b420000b0a09afc0`,
computed with Python `hashlib` during development), program-part word
layout, and padding. The validate test mirrors every Zig test in
`dxil_validate.zig` plus part-count 256, no-DXIL-part, short DXIL part,
bad bitcode magic, offset-out-of-bounds, and error-result field rules.
The builder/serialize test decodes serialized bitcode with a test-side
bit reader and asserts exact record order, SETTYPE transitions, integer
encodings (-1 -> 1, 7 -> 14, 1.5f -> `0x3FC00000`), the struct
named-then-anon record pair, 1-based metadata node operands with null
staying 0, isproto flags, and symtab entries. The mapping/toolchain
test covers every table row in `parity-test.md` sections 7-9 and the
DXC runner against fake executables (success writes the output file,
exit 3 yields `InvalidIr` mentioning `exit code 3` and the stderr
detail, a missing executable yields `ShaderToolchainUnavailable`).

### 2. UBSan

Command:

```sh
make check-ubsan
```

Result: PASS. Same ten binaries, same 955 checks, built with
`-fsanitize=undefined` added to the strict flags. No sanitizer report
appeared.

### 3. ASan + UBSan

Command:

```sh
make check-asan
```

Result: PASS. Same ten binaries, same 955 checks, built with
`-fsanitize=undefined,address`. No sanitizer report appeared. This is
a Linux ASan run only; no Mac ASan result is claimed.

### 4. Mechanical style check

The repo's `runtime/c/tools/check_style.py` only scans git-tracked
files under a repository's `runtime/c`, so it was run from a scratch
git repository containing verbatim copies of this directory's `src/`,
`tests/`, and `Makefile` under `runtime/c/dxil/`, plus the unmodified
checker scripts:

```sh
python3 runtime/c/tools/check_style.py
```

Result: `C style check: 57 sources, 0 failures; line limit 999`
(re-run for this round after adding the IR, native, HLSL, emit,
analyze, and translate sources plus the four new test files).

The first run reported 6 failures (orphan-test rule: the Makefile
listed test names without the `.c` suffix); the Makefile now names
each test source file explicitly and the re-run passes. The scratch
copy is under `/tmp/dxil-style-check` (ephemeral, not a deliverable).

### 5. Compilation flags

Every source and test compiles with:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror
```

No warnings. Largest C file is `src/dxil_hlsl_expr.c` at 857 lines;
all files are under the 999-line limit. The analyzer was split across
five files (`dxil_analyze*.c`, 536-791 lines each) plus the internal
header to stay under the limit, and the test-side bitcode scanner
lives in the shared `tests/test_bitcode_scan.h` (the Makefile test
rule now depends on it, so header edits rebuild the tests).

## What was NOT tested (see gaps.md)

- No Zig reference output: no Zig toolchain exists on this VM, so no
  output was diffed against Zig-generated bytes; expectations were
  hand-derived from Zig semantics plus oracle tests.
- No real DXC binary: the runner and toolchain translate path were
  tested only with fake scripts.
- No GPU execution, no macOS run, no Windows run, no release
  qualification, no Doe-versus-Dawn comparison.
- Container/part sizes near `UINT32_MAX` are guarded but not exercised.
