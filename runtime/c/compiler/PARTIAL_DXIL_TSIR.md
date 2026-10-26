# PARTIAL DXIL and TSIR C parity, not full parity

This commit adds a partial, tested snapshot of DXIL and TSIR C code to
`runtime/c/compiler/src/dxil/` and `runtime/c/compiler/src/tsir/`.
It is NOT full Zig to C parity, and it must not be presented as such.

## What is included, frozen snapshot

Source tree for this snapshot: `~/workspace/part1-snapshot/` in the
coordinator workspace, copied from `~/workspace/doe-c-work/dxil/` and
`~/workspace/doe-c-work/tsir/` at the time of this commit. Workers were
still editing those directories, so live counts have since moved; the
counts below are the frozen snapshot counts, verified by the coordinator.

- DXIL: 45 C/header files, 11,402 lines, in the snapshot.
  Pushed here, minus the excluded files below: 19 .c files, 17 .h files,
  6 test .c files, plus test_util.h, gaps.md, parity-test.md,
  test-results.md. Total pushed DXIL C/header files: 43, plus docs.
  DXIL tests in this tree: 6 programs, 745 checks, 0 failures, Linux
  GCC 13.3.0, standalone flags.
- TSIR: 41 C/header files, 13,082 lines, in the snapshot.
  Pushed here: 20 .c files, 16 .h files, 3 test .c files, plus
  test_common.h, gaps.md, parity-mapping.md, parity-test.md,
  test-results.md. Total pushed TSIR C/header files: 40, plus docs.
  TSIR tests in this tree: 3 programs (sha_scalar, digest, planner),
  179 checks, 0 failures, Linux GCC 13.3.0, with ASan/UBSan.
- Combined pushed C/header files: 83, plus docs and Makefile targets.
  Combined passing test checks in this tree: 924 (745 DXIL + 179 TSIR).

## What is excluded, exactly

- CSL entirely: `runtime/zig/src/compiler/wgsl/emit/csl/` (37 files,
  13,508 lines) and `runtime/zig/src/spatial/csl/` (8 files, 2,525
  lines), plus the 7 CSL-only TSIR files. No CSL code is in this commit.
  The local CSL output at `~/workspace/doe-c-work/csl/` (12 files,
  859 lines, written before the NO CSL correction) was NOT copied here.
- DXIL: `dxil_translate.c` and `dxil_translate.h` are excluded.
  `dxil_translate.c` in the snapshot includes `dxil_analyze.h`, which
  does not exist in the snapshot; the WGSL analyzer it needs is still
  being built by the DXIL worker. Including it would break compilation.
- TSIR: `test_reference_emit.c` is excluded. With all TSIR sources
  compiled together, that test fails 2 of 92 checks
  (test_reference_emit.c:865,869: kernel-body status got 2, INVALID,
  want 3, UNSUPPORTED, for a NULL input). The test expectation is stale
  relative to the new kernel-body implementation; it is NOT pushed as
  passing, and fixing that expectation/behavior is Part 2 work.
- TSIR standalone `tests/Makefile` is excluded; its SRC list is stale
  (it omits the new extended, frontend, kernel-body, transcendental,
  and WGSL IR sources, and its old-source-only build fails to link).
  Tests in this tree are named by `runtime/c/compiler/Makefile`
  targets `test-dxil-tsir-partial` and `test-dxil-tsir-sanitize`.

## What is NOT yet wired, and why

- These sources are NOT yet added to `src/sources.txt`, so they are
  NOT yet linked into `libdoe_compiler.so`. That wiring was attempted
  and rejected for this commit because many of these files fail the
  compiler owner's strict flags (`-Wconversion`, `-Wshadow`,
  `-Wstrict-prototypes`, `-Wmissing-prototypes`, `-Wswitch-enum`,
  `-Werror`), for example `dxil_builder.c` and `dxil_mapping.c` on
  `-Wswitch-enum`, and `tsir_planner.c`, `tsir_scalar.c`,
  `tsir_frontend.c` on conversion/missing-prototype rules.
  Adding them to `sources.txt` would break the compiler lib build and
  the aggregate check. Fixing those strict-flag issues and then adding
  them to `sources.txt` is Part 2 work, listed in the commit message
  as remaining, not hidden.
- The compiler lib build itself (`make -C runtime/c/compiler all`)
  is unchanged by this commit, because `sources.txt` is unchanged,
  and it still passes. The new tests use their own
  `PARTIAL_CFLAGS` (standalone C11 flags, matching the workers'
  flags), documented here, not the compiler owner's strict flags.
- Integrated tree results verified by the coordinator on Linux
  GCC 13.3.0 before this push: `make -C runtime/c/compiler
  test-dxil-tsir-partial` 924 checks, 0 failures;
  `test-dxil-tsir-sanitize` (ASan/UBSan) same 924 checks, 0 failures;
  `make -C runtime/c/compiler all` passes;
  `python3 runtime/c/tools/check_style.py` 1305 sources, 0 failures
  (up from 1222, plus the 83 C/header files in this commit);
  `make -C runtime/c check` passes, including check-build for all
  owners and check-globals (333 artifacts, 11 writable objects,
  0 failures). That is the runtime/c aggregate defined by
  `runtime/c/Makefile`, not a claim about any other aggregate,
  GPU, or release qualification.
- No Mac ASan result, no Doe advantage over Dawn, no GPU, release,
  or full-browser qualification is claimed. The Linux-GCC archive
  gate that stops the aggregate, and the Mac ASan hang on the user's
  Mac, remain unresolved and are not claimed here.

## Gaps still open, from the workers' gaps.md files

- DXIL: native IR walk, primary emit path, and translate entry
  points are in progress in this snapshot (ir, hlsl, native, emit
  files are present and compile, but translate is excluded above and
  full parity is not yet established). See `src/dxil/gaps.md`.
- TSIR: WGSL-IR frontend, kernel-body emitters, extended-op
  interpreter, and bootstrap tools JSON layer are in progress in
  this snapshot; see `src/tsir/gaps.md`. The 103-row capability
  registry was not found in the repo tree; the registry mapping
  from the repo tree is Part 2 work, with missing rows listed as
  missing, never invented.
- Fawn launcher gap (installed Mac launcher defaults to the Zig
  library while repository scripts select C) is untouched by this
  commit, and remains last, as its own small commit, if still present.

## How to run the tests in this tree

```
make -C runtime/c/compiler test-dxil-tsir-partial
make -C runtime/c/compiler test-dxil-tsir-sanitize
make -C runtime/c/compiler all
python3 runtime/c/tools/check_style.py
```
