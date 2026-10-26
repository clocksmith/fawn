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
| emit_kernel_body.zig (896) | done, non-CSL only | `tsir_kernel_body.c` (758 lines): WGSL, MSL, DXIL, SPIR-V bodies for fused_gemv, rms_norm, gather, exact texts as read from Zig; all other ops and the CSL backend return TSIR_ERR_UNSUPPORTED. CSL bodies are quarantined, not built. Literal epsilon uses a shortest-round-trip f64 writer; extreme plain-decimal cases are cross-checked (see G-EPSILON-FORMAT). |
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
| family_hint.zig (183) | done | `tsir_frontend.c` pure predicate plus `tsir_family_hint.c` `tsir_family_hint_infer_ir` (indexed-access coverage, indirect-access detection with the Zig depth cap 8 for local aliases) |
| frontend.zig (885) | done | `tsir_frontend_lower.c`, `tsir_frontend_bindings_axes.c`, `tsir_frontend_axes.c`, `tsir_frontend_reductions.c`, plus `tsir_wgsl_ir.c` (C mirror of `../wgsl/ir/ir.zig`, `ir_query`, `layout_utils`). Per-function bindings, axes, reductions, collectives, rejections, family hint, semantic body; contract_version 1. Approximations: manually constructed OTHER without metadata uses "other" (see G-IR-OTHER); family-hint expression graphs use an explicit work stack (see G-DEPTH-GUARD). |
| frontend_body.zig (178) | done | `tsir_frontend_passes.c` body inference, including RMSNorm epsilon inference (uniform path `uniform:u.eps` with std430 field offset, or literal fallback) |
| frontend_collectives.zig (246) | done | `tsir_frontend_passes.c` collectives walker (statement-walk order, switch expressions only, axis from innermost recognized for loop or -1) |
| frontend_expr.zig (637) | done | `tsir_frontend_expr.c` literal/symbolic upper bounds, step, init bound, dispatch-guard scan, step-sign detection, dispatch-axis letter |
| frontend_rejections.zig (122) | done | `tsir_frontend_passes.c` rejections walker (while loop, unstructured loop, decreasing for loop; `.body[i]`/`.then`/`.else` path segments) |
| frontend_types.zig (27) | done | `tsir_frontend.c` scalar mapping |
| mod.zig (57) | implemented | `tsir.h` re-exports the non-CSL public owners; both standalone tools consume the facade. |
| planner.zig (295) | done | `tsir_planner.c` |
| planner_support.zig (47) | done | `tsir_planner.c` |
| reference_extended_ops.zig (841) | done, with transcendental caveat (see G-TRANSCENDENTAL-BIT) | `tsir_extended.c`, `tsir_extended_ops.c`: identity, residual_add, gated (gelu/silu/sigmoid), attention_scores, l2_normalize, conv1d_depthwise all implemented and tested; linear_attention dispatch preserves Zig's fall-through (see G-LINEAR-DEAD-PATH) and its math is implemented separately as `tsir_linear_attention_step` and tested directly. Transcendentals follow pinned Zig f32 paths; see G-TRANSCENDENTAL-BIT. |
| reference_inputs.zig (26) | done | `tsir_scalar.c` |
| reference_interpreter.zig (762) | done | `tsir_reference.c`, `tsir_reference_bodies.c`, `tsir_extended.c`, `tsir_extended_ops.c`: full dispatch order including extended ops; transcendental table metadata retained, plus pinned exp/tanh and existing sqrt evaluation (see G-TRANSCENDENTAL-BIT) |
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

- G-REGISTRY: `config/webgpu-capability-inventory.json` exists with 103
  capability rows, but has no exhaustive C-versus-Zig parity mapping. This
  TSIR-local inventory is not a substitute. `runtime/c/native/README.md`
  reconciles 266 retained lookup names versus 250 C names: the remaining 15
  foreign-delegation-only names and one logging no-op are not 16 working
  retained native features waiting to be ported. Actual native/browser
  functionality and physical qualification remain independent gates.
- G-DESCRIPTOR-HASH: resolved for low-level planning as well as bootstrap.
  `TsirTargetDescriptor` now carries the complete retained correctness fields;
  `TsirTargetIdentity` aliases it. Planning computes the shared target hash
  before allocating results, and rejects invalid descriptors with empty output.
  This changes the source contract: old reduced manually constructed descriptors
  must supply name, streaming-GEMM tag and the complete correctness fields.
  In-tree initializers and consumers were updated; identity-only mutations alter
  the stored hash and mismatched emitter identities reject.
- G-F64: Shared tsir_format.c now matches 4,152 actual Zig 0.15.2 plain-decimal outputs, including signed zero, subnormals and extreme finite values. Nonfinite rejection preserves errno/fenv including signaling NaNs. This sample is not exhaustive over binary64.

- G-EMITTER-DIGEST: resolved for all four non-CSL emitters through
  `tsir_emitter_code_digest`, including DXIL outside the three-target bootstrap
  registry. Seven actual Zig comparisons cover three descriptors and four
  emitter source concatenations; eleven bounded source/error controls also pass.
  This identifies retained reference inputs, not C source. Missing or oversized
  inputs reject; no Zig executable is needed by C tools. E23 live configuration
  knobs are consumed only by retained CSL bodies and remain excluded, not an
  unimplemented non-CSL feature.
- G-SOFTMAX-TRAP: resolved by a retained-reference safety correction, not by
  adding a C trap. Zig scalar-fold helpers now report `InvalidReductionOp` for
  the compound `softmax_stable` operation and generic interpretation propagates
  it; C retains `TSIR_ERR_INVALID`. Neither returns a fabricated scalar value.
  This deliberately changes the old Zig invalid-input contract; valid folds
  are unchanged and the whole retained WGSL suite passes after the correction.
- G-STRINGS: resolved by length-bearing schema/IR/descriptor fields and span
  entry points. Positive `*_length` selects exact caller-owned bytes, including
  embedded NUL and nonterminated slices; zero retains legacy `strlen`, and an
  empty value remains `""`. Caller storage must cover the declared span and
  remain alive for the synchronous call. Frontend/planner outputs own duplicated
  bytes and lengths. Canonical JSON, identities, name/role comparisons, bound
  construction, epsilon lookup and emitter metadata preserve lengths. Six actual
  Zig slice comparisons cover semantic, realization, emitter-version, manifest,
  descriptor and skeleton identities. This is independent of WGSL source syntax,
  whose identifier syntax does not admit embedded NUL.
- G-MINMAX-NAN: The pinned Linux x86-64 reference drops one NaN and selects the second payload for two NaNs, without FE_INVALID. The AArch64 reference instead uses native minimum/maximum-number behavior: signaling NaNs quiet and raise FE_INVALID, and quiet payload selection follows that target. C implements both target paths. Signed-zero ties retain numeric-only comparison where the reference does not promise their sign.

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
- G-TRANSCENDENTAL-BIT: exp/tanh now use source-faithful Zig 0.15.2 f32 algorithms with retained MIT/Sun notices; sqrt retains its existing path. A direct differential checks 470 runtime inputs and 36 min/max pairs: 3,354 bit-exact comparisons and eight explicitly qualified signed-zero ties. This is not an exhaustive input or whole-interpreter proof. Header-only declared log/sin/cos/tan/rsqrt/recip are not added as unused alternate engines.

- G-EPSILON-FORMAT: The shared formatter replaces the truncated fixed buffer and incorrect exponent switch. All four non-CSL epsilon emitters and canonical identities pass the same 4,152 actual Zig decimal cases. CSL formatting remains excluded.

- G-IR-OTHER: Source adaptation retains actual sampler/texture/storage-texture tag names for shared retained types; five actual Zig semantic digest/rejection cases pass. Manually constructed OTHER types without metadata still use the generic other detail. C-only type kinds without retained Zig equivalents are not claimed equivalent.

- G-DEPTH-GUARD: resolved for the formerly truncated family-hint expression
  predicates by `tsir_family_hint.c` work stacks and memoized graph queries.
  A 4,096-level shared DAG retains axis evidence; traversed malformed edges and
  cycles fail explicitly rather than silently classifying a different family.
  Global-base lookup is bounded by graph size, not 512. The retained eight-local
  alias bound is unchanged and tested at both sides. This does not remove source
  adapter pool limits: those are explicit error boundaries, not silent truncation.
- G-MANIFEST-PIPELINE: real source lowering, planning and identity generation
  now execute through shared C owners. The adapter has explicit compiler pools
  and a 1 MiB source cap. Constants, aliases, symbolic overrides, private and
  workgroup roots now use the compiler's existing declaration/specialization
  owners. Switch, continuing, break-if and phony assignment are projected like
  retained Zig TSIR; the projection is not an executable general WGSL IR.
  Five mutated global-declaration fixtures compare exactly against Zig.
  Packed-integer builtin expansion and ldexp argument materialization now
  have actual shared semantic differential coverage. The two formerly different
  admissions (inferred integer-vector ldexp and local-const packed unpack) now
  match after narrow retained Zig abstract-type/constant-binding corrections;
  pre-change rejection receipts are preserved separately. The canonical
  source differential matches against that explicitly changed reference; this is not
  a claim that the old reference already accepted them. General WGSL-IR structural
  identity is not established; immediate roots still reject because retained TSIR has no such
  address-space representation. Wider WGSL equivalence remains a separate gate.
  Nested member/index types are projected from canonical aggregate metadata,
  not potentially uncached intermediate AST type slots; vector multi-swizzles
  project value-loaded bases. Direct struct/vector/matrix/array controls and
  actual nested-source differentials exercise that distinction.
  No source-pattern classifier or Zig fallback substitutes for these paths.
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
