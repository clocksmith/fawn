# TSIR gaps: unimplemented or unverified work

This file lists everything NOT done, so the coordinator never has to
guess from the passing tests. A test that asserts
`TSIR_ERR_UNSUPPORTED` for a row below is evidence that the gap fails
visibly; it does not close the gap.

## Count correction (verified, not assumed)

The task brief said 38 Zig files in `tsir/` and 40 total. The tree at
head `43040741d` has 36 files in `tsir/` (9,923 lines) and 2 in
`tools/` (751 lines): 38 files, 10,674 lines total. Line counts match
the brief exactly; file counts were off by two, and the table below
uses the verified 36 + 2.

## Per-file status (Zig file -> C status)

Done means: implemented in `src/`, covered by a passing test, and the
behavior matches the Zig rule as read during this run. Partial means
the named subset only. Missing means the C entry point, where one
exists, returns `TSIR_ERR_UNSUPPORTED` visibly. Quarantined means CSL
code excluded from default parity by the TSIR `CATSCAN.md` and by the
coordinator's clarification that CSL can be ignored; mapped, not built.

| Zig file (lines) | Status | C location / note |
| --- | --- | --- |
| collective_synthesis.zig (144) | done | `tsir_planner.c` collective synthesis |
| digest.zig (999) | done, with one unverified corner (see G-F64) | `tsir_digest.c` (923 lines), `tsir_sha256.c` |
| emit_csl.zig (237) | quarantined, not built | CSL emitter; see mapping E18 |
| emit_csl_f16_pack.zig (48) | done (text helpers only) | `tsir_emit.c` |
| emit_dtype_routing.zig (73) | done | `tsir_emit.c` |
| emit_dxil.zig (71) | done | `tsir_emit.c` skeleton plus `tsir_kernel_body.c` DXIL bodies for fused_gemv, rms_norm, gather |
| emit_kernel_body.zig (896) | done, non-CSL only | `tsir_kernel_body.c` (758 lines): WGSL, MSL, DXIL, SPIR-V bodies for fused_gemv, rms_norm, gather, exact texts as read from Zig; all other ops and the CSL backend return TSIR_ERR_UNSUPPORTED. CSL bodies are quarantined, not built. Literal epsilon uses a shortest-round-trip f64 writer; scientific-exponent form is unasserted (see G-EPSILON-FORMAT). |
| emit_kernel_body_attention.zig (443) | quarantined, not built | CSL-only attention body |
| emit_kernel_body_conv1d.zig (110) | quarantined, not built | CSL-only |
| emit_kernel_body_gated.zig (115) | quarantined, not built | CSL-only |
| emit_kernel_body_kv.zig (245) | quarantined, not built | CSL-only |
| emit_kernel_body_l2_normalize.zig (73) | quarantined, not built | CSL-only |
| emit_kernel_body_linear_attention.zig (169) | quarantined, not built | CSL-only |
| emit_kernel_context.zig (93) | done | `tsir_emit.c` (config, role/index helpers, CSL text helpers) |
| emit_msl.zig (71) | done | `tsir_emit.c` skeleton plus `tsir_kernel_body.c` MSL bodies for fused_gemv, rms_norm, gather |
| emit_semantic_metadata.zig (91) | done | `tsir_emit.c` |
| emit_spir_v.zig (71) | done | `tsir_emit.c` skeleton plus `tsir_kernel_body.c` SPIR-V bodies for fused_gemv, rms_norm, gather |
| emit_text_skeleton.zig (132) | done | `tsir_emit.c`: header, metadata, realization-only body_comment, and semantic-aware path (header, metadata, kernel body; no partial text on failure) |
| emit_webgpu.zig (141) | done | `tsir_emit.c` realization-only skeleton plus `tsir_kernel_body.c` WGSL bodies for fused_gemv, rms_norm, gather |
| family_hint.zig (183) | done | `tsir_frontend.c` `tsir_family_hint_classify` (pure predicate, retained) plus `tsir_family_hint_infer_ir` (indexed-access coverage, indirect-access detection with the Zig depth cap 8 for local aliases) |
| frontend.zig (885) | done | `tsir_frontend_lower.c`, `tsir_frontend_bindings_axes.c`, `tsir_frontend_axes.c`, `tsir_frontend_reductions.c`, plus `tsir_wgsl_ir.c` (C mirror of `../wgsl/ir/ir.zig`, `ir_query`, `layout_utils`). Per-function bindings, axes, reductions, collectives, rejections, family hint, semantic body; contract_version 1. Approximations: IR OTHER-type detail is "other" (see G-IR-OTHER); general tree walks have a 512 depth guard (see G-DEPTH-GUARD). |
| frontend_body.zig (178) | done | `tsir_frontend_passes.c` body inference, including RMSNorm epsilon inference (uniform path `uniform:u.eps` with std430 field offset, or literal fallback) |
| frontend_collectives.zig (246) | done | `tsir_frontend_passes.c` collectives walker (statement-walk order, switch expressions only, axis from innermost recognized for loop or -1) |
| frontend_expr.zig (637) | done | `tsir_frontend_expr.c` literal/symbolic upper bounds, step, init bound, dispatch-guard scan, step-sign detection, dispatch-axis letter |
| frontend_rejections.zig (122) | done | `tsir_frontend_passes.c` rejections walker (while loop, unstructured loop, decreasing for loop; `.body[i]`/`.then`/`.else` path segments) |
| frontend_types.zig (27) | done | `tsir_frontend.c` scalar mapping |
| mod.zig (57) | implemented | `tsir.h` re-exports the non-CSL public owners; both standalone tools consume the facade. |
| planner.zig (295) | done | `tsir_planner.c` |
| planner_support.zig (47) | done | `tsir_planner.c` |
| reference_extended_ops.zig (841) | done, with transcendental caveat (see G-TRANSCENDENTAL-BIT) | `tsir_extended.c`, `tsir_extended_ops.c`: identity, residual_add, gated (gelu/silu/sigmoid), attention_scores, l2_normalize, conv1d_depthwise all implemented and tested; linear_attention dispatch preserves Zig's fall-through (see G-LINEAR-DEAD-PATH) and its math is implemented separately as `tsir_linear_attention_step` and tested directly. Transcendentals use double-precision evaluation rounded to f32 (`tsir_transcendental.c`), not Zig's declared minimax polynomials. |
| reference_inputs.zig (26) | done | `tsir_scalar.c` |
| reference_interpreter.zig (762) | done | `tsir_reference.c`, `tsir_reference_bodies.c`, `tsir_extended.c`, `tsir_extended_ops.c`: full dispatch order including extended ops; transcendental table metadata retained, plus double-precision exp/tanh/sqrt evaluation (see G-TRANSCENDENTAL-BIT) |
| reference_reduction.zig (497) | done | `tsir_reference.c` generic rank path (ranks 1, 2, 3, 4+) |
| reference_scalar.zig (113) | done | `tsir_scalar.c` |
| reference_types.zig (18) | done | `tsir_reference.h` result type |
| schema.zig (767) | done | `tsir_schema.h`, `tsir_schema.c` |
| tools/tsir_bootstrap_manifest_inputs.zig (220) | implemented for the non-CSL bootstrap inputs | `tsir_bootstrap_manifest.c`, `tsir_source*.c`, `tsir_targets.c`, and `tools/tsir_bootstrap_manifest_inputs.c`: actual shared C parser/sema, typed adaptation, TSIR lowering/planning/digests; nine entries match actual Zig output byte for byte. See source-adapter limits below. |
| tools/tsir_bootstrap_oracle.zig (531) | implemented and exercised | `tsir_bootstrap_oracle.c` and standalone `tools/tsir_bootstrap_oracle.c`: argv, bounded reads, reference execution, exact status JSON and process-failure boundaries. 67 independent/differential checks against Zig 0.15.2. |

The three previously partial non-CSL inventory entries now have executable C
replacements and focused acceptance evidence. This file inventory does not prove
universal runtime parity. The original 28 implemented entries retain their
qualifications below; all seven CSL-only TSIR files remain excluded.
See [bootstrap evidence and limits](../../TSIR_BOOTSTRAP.md).

## Open items that no code in this directory resolves

- G-REGISTRY: the 103-row capability registry and the 266-name native
  API inventory were not found in this tree. No registry-row mapping
  is claimed. The coordinator must supply the inventory file (reported
  to live on the user's Mac) before that mapping can be written.
- G-DESCRIPTOR-HASH: resolved for the three non-CSL bootstrap targets by
  `tsir_targets.c`; the manifest pipeline installs actual correctness hashes
  into planned functions. The older low-level planner API still accepts its
  reduced descriptor, so standalone callers must supply identity explicitly.
- G-F64: Zig f64 `{d}` formatting is verified only for the plain
  cases in the tests (0.5-style values, 1.5, 0.000001, integers).
  Scientific-notation exponents, negative zero, and other exotic
  values need a Zig cross-check before any manifest digest is
  compared byte-for-byte against Zig output. The C writer is
  deterministic and round-trips, but determinism is not Zig equality.
- G-EMITTER-DIGEST: bootstrap manifests now hash the actual retained
  reference emitter source concatenations, exactly matching the format's Zig
  reference identity. This identifies those reference inputs, not C source.
  Missing or oversized inputs reject; no Zig executable is needed by C tools.
- G-SOFTMAX-TRAP: Zig traps on softmax_stable reaching the scalar
  fold helpers; C returns TSIR_ERR_INVALID. Neither produces a
  value, but the observable behavior differs (trap vs error code).
  If the coordinator needs trap-identical behavior, say so; the C
  choice was made because a library must not trap on bad input.
- G-STRINGS: the C API uses NUL-terminated strings, so TSIR names
  containing a NUL byte cannot be represented. Zig slices can contain
  them. No test covers that case because the API cannot express it.
- G-MINMAX-NAN: C min/max combine propagates NaN, matching the Zig
  scalar helper's documented propagate contract. If Zig's `@min`/`@max`
  on the coordinator's toolchain instead follow C `fmin`/`fmax`
  (NaN-dropping) semantics, the rank-1 min/max NaN tests would need
  revisiting. The tests assert propagation, per the Zig comment.
- G-GEMV-NANINF: Zig GEMV code does not check `nan_inf`, although
  its docstring summary mentions propagate. C follows the code, not
  the docstring. If the docstring is the intended contract, GEMV
  needs a nan_inf check added in both implementations' tests.
- G-NO-ZIG-CROSSCHECK: superseded for bootstrap manifests, target/emitter
  identities and the 67 oracle cases by actual pinned Zig 0.15.2 differential
  checks on Mars. This does not establish untested interpreter/emitter corners.
- G-NO-GPU: nothing here was run on a GPU, and emitted skeleton text
  is contract text, not a compilable module. No Doe-over-Dawn, release,
  or browser qualification claim is made or implied.
- G-LINEAR-DEAD-PATH: Zig's tryLinearAttention reads its read-write
  state binding through inputBytesForReadOnlyBinding, which returns
  null for any read-write binding, so the Zig interpreter path always
  falls through for its own declared shape. C preserves that
  fall-through in dispatch (TSIR_ERR_UNSUPPORTED through
  tsir_reference_run) and tests the state-update math directly
  through tsir_linear_attention_step. This is a faithful
  reproduction of observed Zig behavior, not a redefinition; if the
  coordinator fixes the Zig input convention, the C dispatch must be
  revisited.
- G-TRANSCENDENTAL-BIT: the Zig interpreter header declares
  exp/log/sin/cos/tan/tanh/rsqrt/recip as sollya-bounded minimax
  polynomials with ULP bounds (1,1,1,1,2,2,1,1) and states libm is
  NOT used, but the Zig source in this tree calls std.math.exp/
  tanh/sqrt, whose exact bit output cannot be established from this
  VM (no Zig toolchain, no Zig binary). C evaluates exp/tanh/sqrt
  in double precision and rounds to f32 (within about 1 ULP of the
  true value). Tests assert exactly-representable cases bit-exactly
  and everything else within a stated tolerance. Bit parity for
  every transcendental-bearing op (gated, attention_scores,
  l2_normalize, linear_attention step) stays unestablished pending
  a Zig cross-check. log/sin/cos/tan/rsqrt/recip have no C function.
- G-EPSILON-FORMAT: kernel-body literal epsilon uses a
  shortest-round-trip f64 writer in Zig {d} style. Tests assert
  only plain-decimal values (0.00001, 0.5); the scientific-exponent
  form was not cross-checked against a Zig binary and must not be
  asserted as Zig-equal without one. CSL attention scale and l2_eps
  used Zig {e}; those paths are quarantined and unbuilt.
- G-IR-OTHER: the C WGSL IR mirror collapses type kinds the frontend
  never distinguishes (samplers, textures, storage textures) into
  TSIR_IR_TYPE_OTHER. A global of that kind gets the binding-type
  rejection with detail "other" instead of Zig's specific type tag.
  Documented approximation, not a silent mismatch: the rejection
  reason and path match, the detail string differs.
- G-DEPTH-GUARD: general IR tree walks in the C frontend carry a
  512 depth guard (the Zig walkers are recursive without an explicit
  cap in the code read). A tree deeper than 512 levels stops
  descending instead of recursing unboundedly; no such tree was
  observed in the fixtures. Local-alias indirect-access detection
  keeps Zig's exact depth cap 8.
- G-MANIFEST-PIPELINE: real source lowering, planning and identity generation
  now execute through shared C owners. The adapter has explicit compiler pools
  and a 1 MiB source cap. Module constants/memory roots, switch/continuing and
  unsupported expressions reject with diagnostics; no source-pattern classifier
  or Zig fallback substitutes for these paths. These limits do not affect the
  three bootstrap fixtures. Wider WGSL equivalence remains a separate gate.
- G-ORACLE-CLI: resolved by the standalone C CLI, including accepted-and-ignored
  provenance flags, first errors and process failures. Shape/byte overflow and
  f32/f16 conversion boundaries are covered against retained Zig.

## Original Linux worker scope (historical)

- Nothing was pushed to GitHub (task forbids it; the coordinator
  merges and pushes).
- Nothing was written into `~/workspace/fawn-tree` (read-only source).
- No CSL emitter or CSL body was built (quarantined, coordinator
  clarified CSL can be ignored for parity).
- No stub was presented as done: every missing behavior returns
  `TSIR_ERR_UNSUPPORTED` (or `TSIR_ERR_INVALID` for the softmax trap
  case) and is listed above.
- No Mac ASan, Dawn comparison, or qualification results are claimed.
  All runs are Linux GCC + ASan/UBSan on this VM; see test-results.md.
