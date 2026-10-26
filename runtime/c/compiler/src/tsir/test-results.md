# TSIR test results

Environment: Linux VM, GCC 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1).
Flags for every build: `-std=c11 -Wall -Wextra -Wpedantic -Werror
-fsanitize=address,undefined -fno-sanitize-recover=all -g`, linked
with `-lm`. No Zig toolchain, GPU, or network was used. Nothing was
pushed to GitHub and nothing was written into `~/workspace/fawn-tree`.

## Exact commands

From `~/workspace/doe-c-work/tsir/tests/`:

```
make clean
make check
```

`make check` builds all four test binaries from `../src/` (see the
Makefile's SRC list) and runs each one. Any failing check prints a
`FAIL` line with file and line, and `make check` stops at the first
failing binary.

## Results (final run, after the 999-line split of tsir_reference.c)

| Binary | Checks | Failures | Result |
| --- | --- | --- | --- |
| test_sha_scalar | 81 | 0 | pass |
| test_digest | 29 | 0 | pass |
| test_planner | 69 | 0 | pass |
| test_reference_emit | 92 | 0 | pass |
| Total | 271 | 0 | pass |

Output of the final run:

```
./test_sha_scalar
test_sha_scalar: 81 checks, 0 failures
./test_digest
test_digest: 29 checks, 0 failures
./test_planner
test_planner: 69 checks, 0 failures
./test_reference_emit
test_reference_emit: 92 checks, 0 failures
```

## What each binary covers (mapping to parity-test.md)

- test_sha_scalar: T-SHA-1..4 (empty, "abc", one-million-"a",
  streaming split), T-SCHEMA-1..3 (byte sizes, name tables,
  exactness well-formedness), T-SCALAR-1..9 (f32/f16/bf16 round-trips
  and RNE ties, expected bytes incl. overflow, identities, combine
  with NaN propagation, integer unsupported, read-only lookup via
  the reference tests).
- test_digest: T-DIGEST-1..8 (empty semantic exact string,
  one-function exact string, escaping incl. `\u0001`, optional
  `null` fields, NaN rejection, manifest entry order and digest,
  split-digest separation, rmsNorm-only digest restriction).
- test_planner: T-PLANNER-1..12 (tile choice incl. unparsable input,
  PE grid, replicated/sliced/rejected/streamed/host-copied residency,
  reduction and collective synthesis, fabric color exhaustion in
  source order, default emitter params, accumulated rejections).
- test_reference_emit: T-REF-1..15 (empty kernel hash, rejection
  first, rank-1 ops, binomial-vs-linear differing case
  [1, 1e8, -1e8, 1] -> linear 1.0 / binomial 0.0, rank-2/4, GEMV
  [17, 39], RMSNorm literal and uniform-field rejection, gather
  incl. out-of-range unsupported, extended-op unsupported,
  transcendental table metadata), T-EMIT-1..8 (metadata exact
  strings, skeleton header and gates, semantic unsupported, dtype
  routing, context helpers, CSL sqrt_nr and f16 pack loop exact
  text), T-FRONTEND-1..3 (IR scalar mapping, hint classifier,
  lowering unsupported).

## Failures found and fixed during this run (not hidden)

1. bf16 test expectation wrong (not code): 1.01171875 is a tie that
   rounds to even 0x3F82, not 0x3F81. Test corrected; code unchanged.
2. Planner rejection order: collective rejections precede reduction
   rejections in Zig order. Test corrected; code unchanged.
3. Digest escape test pattern: the test's own C string literal
   over-escaped `\u0001`. Test corrected after verifying the
   library's canonical output byte-for-byte with a debug program.
4. `tsir_reference.c` exceeded the 999-line cap (1,336 lines). Split
   into `tsir_reference.c` (633) and `tsir_reference_bodies.c` (708)
   with shared helpers in `tsir_reference_internal.h` (43). All
   tests still pass after the split.

## Style compliance check (final tree)

- Every C source/header in `src/` and `tests/`: at most 999 physical
  lines (largest: `tsir_digest.c` 923, `test_reference_emit.c` 881).
- Every C source/header: `SPDX-License-Identifier: MIT` within the
  first three lines (the Makefile carries it as a comment line 1).
- Every header: `#ifndef DOE_TSIR_*` / `#define` guard.
- No hidden mutable globals in `src/` (all state is caller-owned or
  automatic; name tables are read-only constants).
- No public test-mutation hooks.

## Not established by these results

- Byte equality against Zig output (no Zig cross-check run; see
  gaps.md G-NO-ZIG-CROSSCHECK and G-F64).
- GPU execution, DXIL/CSL/MSL/SPIR-V/WGSL compilation of emitted
  text, Mac ASan, Doe-over-Dawn performance, release qualification,
  or full browser qualification. None is claimed.
- The 103-row registry mapping (registry file not found in this
  tree; see gaps.md G-REGISTRY).
