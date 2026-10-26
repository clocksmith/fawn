# TSIR C parity, non-CSL files: complete, with gaps listed

This commit completes TSIR C parity for all non-quarantined files.
It follows the PARTIAL DXIL and TSIR commit (c7e96c08) and updates
only TSIR. DXIL is not changed by this commit, except that the
Makefile test list now includes the new TSIR tests.

## What is complete

- All 38 Zig TSIR files, minus the 7 quarantined CSL-only files:
  done 28, partial 3, missing 0, quarantined 7, per
  `src/tsir/parity-mapping.md` and `src/tsir/gaps.md`.
- C WGSL IR module (`tsir_wgsl_ir.c/h`), frontend with IR-based
  family hint, kernel-body emitters for WGSL, MSL, DXIL, SPIR-V for
  fused_gemv, rms_norm, gather, extended-op interpreter, and
  bootstrap JSON layer (`tsir_bootstrap.c/h`, `tsir_json.c/h`,
  the repo bounded JSON reader copied verbatim from
  `runtime/c/runtime-cli/src/json.*` at head 43040741d, not hand-rolled).
- Parity-mapping surface: 145 rows, done 134, partial 5, missing 4,
  quarantined 2, plus CSL parts of one partial row.
- Tests, Linux GCC 13.3.0, ASan/UBSan: sha_scalar 81, digest 29,
  planner 69, reference_emit 92, test_new 64, test_new2 53,
  total 388 checks, 0 failures. Two old test expectations were
  updated to the new typed APIs: frontend_lower NULL now
  TSIR_ERR_INVALID, kernel_body NULL now TSIR_ERR_INVALID; the
  unknown-op semantic test still expects TSIR_ERR_UNSUPPORTED.
  Run with: `make -C runtime/c/compiler test-dxil-tsir-partial`
  and `test-dxil-tsir-sanitize`.

## What is still missing or partial, not hidden

- Partial (3): mod.zig (no single umbrella header; coordinator
  decision), bootstrap manifest inputs (entry writer and templates
  only, no WGSL pipeline), bootstrap oracle (library runner only,
  no CLI binary).
- Missing (4 rows on the 145-row surface): registry gap (R0, R0b),
  emitterCodeDigest (E22, no C equivalent, do not hash C sources
  as if they were Zig), config knobs (E23).
- Gaps from `src/tsir/gaps.md`, including: descriptorHash lives
  outside TSIR (zeroed, caller-supplied, no digest equality claimed);
  f64 formatting plain cases only; transcendental bit parity
  unestablished (double-precision exp/tanh/sqrt rounded to f32,
  not Zig's declared minimax polynomials; log/sin/cos/tan/rsqrt/
  recip have no C function); linear_attention dead-path fall-through
  preserved; kernel-body literal epsilon shortest-round-trip f64
  writer, plain-decimal values only, scientific-exponent form not
  cross-checked; IR types the frontend never distinguishes collapse
  to OTHER; general IR walks carry a 512 depth guard; no WGSL
  pipeline behind manifest inputs, so no manifest entry here can
  be claimed to match a Zig lowering run; oracle CLI binary absent;
  no Zig cross-check (no Zig toolchain invoked, no output byte
  string diffed against a Zig binary); no GPU run.
- CSL excluded entirely, as before: 45 files, 16,033 lines, plus
  the 7 CSL-only TSIR files, quarantined, mapped not built.
- Sources are still NOT in `src/sources.txt` and NOT linked into
  `libdoe_compiler.so`, for the same strict-flag reason as in
  PARTIAL_DXIL_TSIR.md. That wiring, with strict-flag fixes, is
  remaining Part 2 work, not claimed here.
- No Mac ASan, Doe advantage over Dawn, GPU, release, or
  full-browser qualification is claimed.
