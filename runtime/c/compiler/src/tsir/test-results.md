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

`make check` builds all six test binaries from `../src/` (see the
Makefile's SRC list) and runs each one. Any failing check prints a
`FAIL` line with file and line, and `make check` stops at the first
failing binary.

## Results (final run, after completing the non-CSL TSIR pieces)

| Binary | Checks | Failures | Result |
| --- | --- | --- | --- |
| test_sha_scalar | 81 | 0 | pass |
| test_digest | 29 | 0 | pass |
| test_planner | 69 | 0 | pass |
| test_reference_emit | 92 | 0 | pass |
| test_new | 64 | 0 | pass |
| test_new2 | 53 | 0 | pass |
| Total | 388 | 0 | pass |

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
./test_new
test_new: 64 checks, 0 failures
./test_new2
test_new2: 53 checks, 0 failures
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
  lowering unsupported; frontend_lower NULL now returns
  TSIR_ERR_INVALID and kernel_body NULL now returns
  TSIR_ERR_INVALID, matching the new typed APIs).
- test_new: T-FIR-1..8 (IR layout sizes/offsets, global-base and
  int-literal queries, binding extraction with unreferenced-global
  exclusion, for-loop axis lower/upper/step, while-loop rejection),
  T-EXT-1..8 (identity byte copy, residual_add, gated silu/gelu,
  attention_scores, l2_normalize, conv1d_depthwise,
  linear_attention step plus dispatch fall-through), T-TRANS
  (exp/tanh/sqrt exact cases and tolerances).
- test_new2: T-KB-1..7 (WGSL fused_gemv and gather exact full text,
  MSL fused_gemv exact full text, SPIR-V gather ternary, WGSL/DXIL
  rms_norm literal and uniform epsilon forms, bad uniform offset
  invalid, CSL backend unsupported, unknown op unsupported,
  semantic-aware DXIL header plus body), T-BOOT-1..4 (manifest
  template and entry writer, bad digest invalid, unknown kernel
  unsupported, oracle fused_gemv values path with exact reference
  hash, rms_norm bytesHex path, gather array-of-buffers path,
  mismatched kernel, malformed JSON, i32 elem not_implemented).

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
5. New-file line caps in this run: `tsir_frontend_lower.c` reached
   1,094 lines and was split into bindings/axes, axes, reductions,
   and orchestration files (all at most 452 lines);
   `tsir_extended.c` reached 1,202 lines and was split into
   `tsir_extended.c` (478) and `tsir_extended_ops.c` (737);
   `test_new.c` reached 1,095 lines and was split into `test_new.c`
   (701) and `test_new2.c` (409).
6. For-loop axis fixture: the loop condition must carry a load of
   the induction local, as in Zig IR; a bare local_ref does not
   match the upper-bound extractor. Fixture corrected; code
   unchanged.
7. Attention fixture expectations: hand-computed outputs corrected
   to [15.378829, 25.378829] (the first draft had an arithmetic
   slip); parity-test.md T-EXT-4 updated to the as-built fixture.
8. L2 fixture collided with the identity path (one read plus one
   write binding, equal shapes), which runs first in Zig too; the
   output shape was changed to [5] so only l2_normalize matches.
   parity-test.md T-EXT-5 notes this.

## Style compliance check (final tree)

- Every C source/header in `src/` and `tests/`: at most 999 physical
  lines (largest: `tsir_bootstrap.c` 925, `tsir_digest.c` 923,
  `test_reference_emit.c` 881, `test_new.c` 701).
- Every C source/header: `SPDX-License-Identifier: MIT` within the
  first three lines (the Makefile carries it as a comment line 1).
- Every header: `#ifndef DOE_TSIR_*` / `#define` guard.
- No hidden mutable globals in `src/` (all state is caller-owned or
  automatic; name tables are read-only constants).
- No public test-mutation hooks.

## Not established by these results

- Byte equality against Zig output (no Zig cross-check run; see
  gaps.md G-NO-ZIG-CROSSCHECK and G-F64). In particular, no emitted
  kernel-body text, reference hash for a transcendental-bearing op,
  or manifest entry has been diffed against a Zig binary.
- Bit parity for exp/tanh/sqrt paths (double-precision evaluation,
  see gaps.md G-TRANSCENDENTAL-BIT) and for the scientific-exponent
  form of literal epsilon (see G-EPSILON-FORMAT).
- The linear_attention interpreter result: Zig's own path falls
  through (see G-LINEAR-DEAD-PATH); only the step math is tested.
- GPU execution, DXIL/CSL/MSL/SPIR-V/WGSL compilation of emitted
  text, Mac ASan, Doe-over-Dawn performance, release qualification,
  or full browser qualification. None is claimed.
- The 103-row registry mapping (registry file not found in this
  tree; see gaps.md G-REGISTRY).
