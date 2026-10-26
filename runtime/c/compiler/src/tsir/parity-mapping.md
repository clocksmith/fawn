# TSIR C-versus-Zig parity mapping

Original mapping source tree (historical worker snapshot): `~/workspace/fawn-tree`, head
`43040741d1d4822d64c8ec8976d6c652ec345242`.
Zig scope verified by this worker before coding:

- `runtime/zig/src/compiler/tsir`: 36 `.zig` files, 9,923 lines, plus `CATSCAN.md`.
- `runtime/zig/src/compiler/tsir/tools`: 2 files, 751 lines
  (`tsir_bootstrap_manifest_inputs.zig` 220, `tsir_bootstrap_oracle.zig` 531).
- Total: 38 files, 10,674 lines. Verified with `ls -1` (36 files in
  `tsir/`, 2 in `tools/`) and `wc -l` (9,923 in `tsir/`, 10,674
  combined). Correction to the task brief: the brief said 38 files in
  `tsir/` and 40 total; the tree at this head has 36 and 38. Line
  counts match the brief exactly; file counts do not, and this
  document uses the verified counts.

Current bootstrap follow-up: see [executable evidence and limits](../../TSIR_BOOTSTRAP.md).
The facade and both non-CSL tools now execute in C; original mapping rows below
retain their unrelated qualifications.

## Registry scope, reconciled against the current tree

`config/webgpu-capability-inventory.json` contains 103 capability rows, but no
exhaustive C-versus-Zig parity classification. The original worker's statement
that this registry was absent is superseded. This document maps the 38-file TSIR
surface, not every native/browser capability. `runtime/c/native/README.md`
reconciles 266 retained names against 250 C names: 15 remaining names have only
foreign delegation or failure, and one logging setter is a no-op. They are not
16 implemented retained native capabilities awaiting C counterparts.

The initial absence of C TSIR was historical input to the original mapping;
current owners, tools, identity and bounded differential coverage are below.

Status words used below, and nowhere else:

- `done`: implemented in `src/`, covered by a test in `tests/`, passing.
- `partial`: implemented for a stated subset only; the subset is named.
- `missing`: not implemented in C. Any C entry point for it returns
  `TSIR_ERR_UNSUPPORTED` visibly; it is never presented as done, and no
  stub emits plausible output.
- `n/a (quarantined)`: CSL-gated Zig code. The TSIR `CATSCAN.md` states
  CSL is an owner-authorized optional quarantine, excluded from default
  native Zig-to-C parity, restored only with explicit
  `-Denable-csl=true`. CSL emitters are therefore mapped but not counted
  as default-parity debt. This matches the later clarification that CSL
  can be ignored for Zig-to-C parity.

C model followed: `runtime/c/STYLE.md`, `runtime/c/README.md`,
`runtime/c/AGENTS.md`, and the naming/ownership conventions of
`runtime/c/compiler/src` (small typed records, explicit ownership,
first error preserved, no hidden mutable globals, no public
test-mutation hooks). C11, four-space indentation, at most 999 lines
per file, SPDX `MIT` within the first three lines, header guards,
`-std=c11 -Wall -Wextra -Wpedantic -Werror`.

## R0: broader registry coverage (not inferred from TSIR tests)

| Row | Zig behavior | Existing C behavior | This run |
| --- | --- | --- | --- |
| R0 | 103-row capability registry | Registry is present; no exhaustive language-parity mapping | not claimed: TSIR coverage does not establish every runtime row. |
| R0b | 266 retained native lookup names versus 250 C names | Reconciled in `runtime/c/native/README.md`: 15 delegation-only/failing entries and one logging no-op | not 16 implemented native features; no success stub is added for symbol-count equality. |


## Schema: schema.zig (767 lines)

`CONTRACT_VERSION = 1`. All enum tag spellings below are the Zig tag
names, which the digest emits verbatim as quoted strings.

| Row | Zig behavior | C status |
| --- | --- | --- |
| S1 | `ExactnessClass`: bit_exact_solo, algorithm_exact, tolerance_bounded | done: `TsirExactnessClass` plus name table |
| S2 | `AlgorithmExactInvariant`: reduction_order, tree_shape, accum_dtype, associativity_grouping | done |
| S3 | `Exactness`: class always declared; invariants only for algorithm_exact, metric/epsilon only for tolerance_bounded | done: struct plus `tsir_exactness_is_well_formed` validation (empty/zero cross-checks) |
| S4 | `RejectionReason` (7): tsir_binding_type_unlowerable, tsir_subgroup_unlowerable, tsir_pe_budget_exhausted, tsir_collective_not_representable, tsir_dependence_unanalyzable, tsir_source_not_affine, tsir_target_unfit | done: enum plus name table |
| S5 | `RejectionEntry`: reason, dot-delimited node_path, short detail | done |
| S6 | `KernelFamilyHint` (14): unknown, elementwise, reduction, gather, rms_norm, fused_gemv, prefill_q4k_gemv, tiled_matmul, attention_decode, attention_tiled, rope, dequant, sample, embed. Hint only, never selects an emitter | done: enum plus name table; selection rule documented, not enforced by C beyond the table |
| S7 | `ScalarKind`: f32, f16, bf16, i32, u32; `byteSize`: 4, 2, 2, 4, 4 | done: `tsir_scalar_byte_size` |
| S8 | `ReductionAssociativity`: strict_ordered, associative_allowed | done |
| S9 | `NanInfPolicy`: propagate, quiet_mask | done |
| S10 | `ReductionOp`: sum, product, min, max, softmax_stable (compound max-then-sum-exp, not a left-fold op) | done: enum; softmax_stable has no scalar identity/combine (see R-ROW below) |
| S11 | `NumericalContract`: accumulation, associativity, nan_inf | done |
| S12 | `CollectiveKind` (13): subgroup_add/min/max/mul/broadcast/ballot/shuffle/exclusive_scan/inclusive_scan, workgroup_barrier, fabric_reduce/broadcast/allreduce | done |
| S13 | `ReductionTreeShape`: binomial, ring, linear | done |
| S14 | `CollectiveSemanticNode`: kind, axis (i32, -1 whole-workgroup), exactness, dtype | done |
| S15 | `CollectiveRealizationNode`: semantic_index, tree_shape, fabric_color (optional), group_size | done |
| S16 | `ReductionRealizationNode`: semantic_index, tree_shape | done |
| S17 | `BufferBinding`: name, group, binding, logical_shape, elem, read_write | done |
| S18 | `IterationAxis`: name, lower_bound, upper_bound, step as strings; "dynamic" placeholder means dependence not analyzable | done: struct (strings are caller-owned views, not copied) |
| S19 | `ReductionRegion`: axis, op (default sum), contract, target_binding | done |
| S20 | `SemanticBodyOp` (14): unknown, fused_gemv, rms_norm, gather, residual_add, gelu_gated, silu_gated, sigmoid_gated, kv_write, kv_read, attention_scores, conv1d_depthwise, l2_normalize, linear_attention | done |
| S21 | `SemanticBindingRole` (27 roles incl. matrix, vector, input, scale, indices, table, output, query/key/value, kv_len_buffer, page_table, summand_a/b, gate, key/value_projection, key/value_cache, decode_position, key/value_output, weight, bias, linear_state) | done |
| S22 | `SemanticAxisRole`: output, reduction, hidden, token, head, query_sequence, key_sequence | done |
| S23 | `SemanticBodyBinding`, `SemanticBodyAxis`: index plus role | done |
| S24 | `RmsNormFormula`: sum_squares_mean_epsilon_rsqrt_scale only | done |
| S25 | `RmsNormReductionTarget`: intermediate_scalar only | done |
| S26 | `RmsNormEpsilonSource`: uniform_field, literal_f32 | done |
| S27 | `RmsNormEpsilon`: source, path (empty for literal), binding_index, byte_offset, literal_f64 (Zig literal_f32 stored as f64 in schema; interpreter casts to f32) | done: `literal_f64` field named for the Zig field; cast rule in reference |
| S28 | `RmsNormBody`: formula, epsilon, hidden_extent_axis, reduction_target | done |
| S29 | `SoftmaxMode`: two_pass_stable, streaming_online | done |
| S30 | `CausalMode`: none, causal, sliding_window | done |
| S31 | `AttentionScoresBody`: softmax_mode, head_dim, key_sequence_axis, scale_source, optional scale binding/offset/literal, has_softcap, causal_mode, optional sliding_window_size, optional query_seq_len | done: schema; extended reference interpretation is mapped below, while retained non-CSL emitters reject this body |
| S32 | `AttentionScaleSource`: uniform_field, literal_f32 | done |
| S33 | `Conv1DDepthwiseBody`: channels, kernel_size, has_bias | done: struct only |
| S34 | `L2NormalizeBody`: hidden, eps (f32) | done: struct only |
| S35 | `LinearAttentionBody`: key_dim, value_dim, key_heads, value_heads, norm_mode, has_dt_bias | done: struct only |
| S36 | `LinearAttentionNormMode`: shared, per_head | done |
| S37 | `SemanticBody`: op, binding_roles, axis_roles, optional rms_norm, attention_scores, conv1d_depthwise, l2_normalize, linear_attention (digest emits rms_norm only; see D-rows) | done: struct; digest restriction preserved, not "fixed" |
| S38 | `SemanticFunction`: name, family_hint, axes, bindings, reductions, collectives, body, source_digest[32] | done |
| S39 | `Semantic`: contract_version (=1), frontend_version ("" pre-versioning default), functions, rejections | done |
| S40 | `ResidencyClass`: pe_replicated, pe_sliced, fabric_streamed, host_copied, recomputed | done |
| S41 | `ResidencyDecision`: binding_index, class, optional axis/shards/fabric_color/chunk_bytes | done |
| S42 | `TileFactors`: per_axis | done |
| S43 | `PEGridShape`: width, height | done |
| S44 | `RealizationFunction`: semantic_index, tiles, pe_grid, residency, collectives, reductions, emitter_params_json, target_descriptor_hash[32] | done |
| S45 | `Realization`: contract_version, functions, emitter_digest[32], rejections | done |
| S46 | `Digests`: semantic, realization, emitter (each 32 bytes) | done |
| S47 | `ManifestLoweringEntry`: 10-field tuple (kernel_ref, backend, target_descriptor_correctness_hash, frontend_version, tsir_semantic_digest, tsir_realization_digest, emitter_digest, compiler_version, exactness, rejection_reasons); non-empty rejections means runtime refuses the backend up front | done: struct plus canonical form |

## Digest: digest.zig (999 lines)

Canonical JSON rules (Zig header): keys in lexicographic byte order of
the camelCase JSON field name, no whitespace, minimal base-10 numbers,
string escape set restricted to `\" \\ \n \r \t \uXXXX`, enums as quoted
snake_case tag names, digests as 64-char lowercase hex, SHA-256 over
canonical UTF-8 bytes. NaN/Inf f64 values are `InvalidSemantic`, never
serialized. Zig f64 formatting is `{d}` (shortest round-trip decimal);
C uses the shared shortest-round-trip formatter, cross-checked against 4,152
actual Zig plain-decimal cases (not exhaustive binary64 coverage). Text fields
carry optional explicit byte lengths; canonical control-byte escaping includes
embedded NUL and no longer truncates at the first zero byte.

| Row | Zig behavior | C status |
| --- | --- | --- |
| D1 | `compute(semantic, realization, emitter_version)`: semantic/realization SHA-256, emitter = SHA-256(emitter_version) | done: `tsir_digests_compute` |
| D2 | `computeWithEmitterDigest`: caller-supplied emitter digest passed through | done |
| D3 | `canonicalizeSemantic` field order: contractVersion, frontendVersion, functions, rejections | done |
| D4 | SemanticFunction order: axes, bindings, body, collectives, familyHint, name, reductions, sourceDigest | done |
| D5 | SemanticBody order: axisRoles, bindingRoles, op, rmsNorm (only when present; other body payloads are NOT emitted by Zig digest) | done: restriction preserved verbatim |
| D6 | Body binding order: bindingIndex, role; body axis order: axisIndex, role | done |
| D7 | RmsNormBody order: epsilon, formula, hiddenExtentAxis, reductionTarget; epsilon order: bindingIndex, byteOffset, literalF32, path, source | done |
| D8 | IterationAxis order: lowerBound, name, step, upperBound | done |
| D9 | BufferBinding order: binding, elem, group, logicalShape, name, readWrite | done |
| D10 | ReductionRegion order: axis, contract, op, targetBinding; contract order: accumulation, associativity, nanInf | done |
| D11 | CollectiveSemanticNode order: axis, dtype, exactness, kind; Exactness order: algorithmExactInvariants, class, toleranceEpsilon, toleranceMetric | done |
| D12 | RejectionEntry order: detail, nodePath, reason | done |
| D13 | Realization order: contractVersion, emitterDigest, functions, rejections | done |
| D14 | RealizationFunction order: collectives, emitterParamsJson, peGrid, reductions, residency, semanticIndex, targetDescriptorHash, tiles | done |
| D15 | TileFactors: perAxis; PEGrid: height, width; ResidencyDecision: axis, bindingIndex, chunkBytes, class, fabricColor, shards | done |
| D16 | CollectiveRealizationNode: fabricColor, groupSize, semanticIndex, treeShape; ReductionRealizationNode: semanticIndex, treeShape | done |
| D17 | `canonicalizeManifestLoweringEntry` order: backend, compilerVersion, emitterDigest, exactness, frontendVersion, kernelRef, rejectionReasons, targetDescriptorCorrectnessHash, tsirRealizationDigest, tsirSemanticDigest | done |
| D18 | `manifestLoweringEntryDigest`: SHA-256 over D17 bytes | done |
| D19 | SHA-256 primitive itself (Zig std.crypto) | done: self-contained `tsir_sha256` in `src/`, verified against NIST vectors (see test-results.md) |

## Frontend: frontend.zig (885) and helpers

The frontend consumes the WGSL IR module type (`../wgsl/ir/ir.zig`,
`ir_query`, `layout_utils`), which is outside the TSIR directory.
That module is now ported as a C mirror in `tsir_wgsl_ir.c`
(types, struct layout, globals, params, locals, expressions,
statements, plus the ir_query helpers the frontend uses). The mirror
covers only the node kinds the frontend walks; type kinds the
frontend never distinguishes collapse to TSIR_IR_TYPE_OTHER (see
gaps.md G-IR-OTHER).

| Row | Zig behavior | C status |
| --- | --- | --- |
| F1 | `lowerIrToTsir`: per-function bindings (only globals actually referenced by that function, with a bound @group/@binding), iteration axes, reductions, collectives, rejections, family hint, semantic body; contract_version 1, frontend_version pinned by caller, source_digest carried per function | done: `tsir_frontend_lower` (bindings/axes in `tsir_frontend_bindings_axes.c` and `tsir_frontend_axes.c`, reductions in `tsir_frontend_reductions.c`, orchestration in `tsir_frontend_lower.c`); returned semantic owns its strings and is freed by `tsir_frontend_free_semantic`. Approximations in gaps.md: G-IR-OTHER, G-DEPTH-GUARD. |
| F2 | `frontend_types.scalarKindFromIr`: f32->f32, f16->f16, i32->i32, u32->u32, abstract_int->i32, abstract_float->f32, bool->u32, void->u32 | done: `tsir_ir_scalar_to_kind` over a C mirror enum of exactly those 8 IR scalar kinds; Zig test cases reproduced in tests |
| F3 | `frontend_types.FrontendError`: OutOfMemory only | done: mapped to TSIR_ERR_NOMEM |
| F4 | `frontend_body.inferSemanticBody` (178 lines): body inference from hint/axes/bindings/reductions | done: `tsir_frontend_passes.c`, including fused_gemv/gather role inference and RMSNorm epsilon inference |
| F5 | `frontend_collectives.collectCollectives` (246 lines) | done: `tsir_frontend_passes.c` collectives walker |
| F6 | `frontend_expr` (637 lines): literal/symbolic upper bounds, step, init bound, dispatch-guard scan, step-sign detection, dispatch-axis letter extraction | done: `tsir_frontend_expr.c` |
| F7 | `frontend_rejections.recoverRejections` (122 lines): typed rejections for unrepresentable constructs, never placeholder values | done: `tsir_frontend_passes.c` rejections walker |
| F8 | `family_hint.infer` (183 lines): reductions present -> fused_gemv only when 2 axes, 1 sum reduction on axis 1, nontrivial bound string (dispatch./uniform:/override:/override@id:/const:), and one indexed load covering both axis locals; else reduction; axes present -> gather when any indirect buffer access, else elementwise; else unknown. Hints are tiebreakers only and must not change feasibility | done: `tsir_family_hint_infer_ir` in `tsir_family_hint.c` (indexed-access coverage, indirect-access detection with Zig's local-alias depth cap 8); the pure predicate `tsir_family_hint_classify` is retained for callers without IR |
| F9 | `frontend_body`/`frontend_expr`/`frontend_collectives` internal helpers | done: ported as part of `tsir_frontend_expr.c` and `tsir_frontend_passes.c` |

## Planner: planner.zig (295), planner_support.zig (47), collective_synthesis.zig (144)

The planner consumes the complete correctness identity in `TsirTargetDescriptor`;
`TsirTargetIdentity` is the same type. Shared `tsir_targets.c` computes each
planned function's hash before publication. Caller-created reduced descriptors
are no longer valid: initialize all correctness fields (including required name
and streaming-GEMM tag), or copy a registered descriptor. The planner-only latency
hint remains unhashed. All in-tree initializers and bootstrap callers use this
contract; hash failure leaves empty output and mismatched emission rejects.

| Row | Zig behavior | C status |
| --- | --- | --- |
| P1 | `planRealization`: per function tiles, residency, collective synthesis, reduction synthesis; emitter_params_json default `{"planner":"tsir.first_fit_v1"}`; target hash from descriptor; contract_version 1; rejections accumulated, first error preserved | done: `tsir_plan_realization` (caller supplies output storage; see header for ownership) |
| P2 | `planTiles`/`chooseTile`: parse upper_bound as u32, unparsable or 0 -> 1; cap 64; floor to power of two | done: `tsir_planner_choose_tile` |
| P3 | `choosePEGrid`: fabric_color_count 0 -> 1x1; else width = min(8, max(1, max_collective_group_size)), height 1 | done |
| P4 | `planResidency`: bytes <= pe budget -> pe_replicated; else slice on largest shape axis (dim > 1) with shards = max(1, pe width), sliced bytes ceil-divided, if fits -> pe_sliced; runtime-sized (zero dim or overflow) -> policy host_copied / fabric_streamed_with_loader (only when loader fabric_streaming and colors > 0) / reject falls through; else if loader streaming and colors > 0 -> fabric_streamed color 0, chunk = min(loader max or 0 -> budget, budget); else rejection tsir_pe_budget_exhausted at `functions[i].bindings[j]` with detail "binding footprint exceeds per-PE budget without legal slice or stream" | done |
| P5 | `estimateBindingBytes`: product of shape dims times elem size; zero dim or overflow -> null (runtime-sized) | done: shared with scalar expected-bytes logic but kept as its own planner helper; overflow returns TSIR_ERR_OVERFLOW visibly in the C byte-count helper, and the planner treats it as runtime-sized only when the Zig rule says null (zero dim or multiply overflow) |
| P6 | `chooseSliceAxis`: largest dim; null when shape empty or best dim <= 1 | done |
| P7 | `chooseStreamChunkBytes`: budget = max(1, pe budget); loader max 0 -> budget, else min | done |
| P8 | `synthesizeReductions`: unsupported accumulation dtype -> tsir_target_unfit rejection ("reduction accumulation dtype is not native on target") and no node; associative_allowed -> linear tree node; strict_ordered -> no node | done |
| P9 | `planner_support.supportsNumericalMode`: f32->f32, f16->f16, bf16->bf16, i32/u32 -> never | done |
| P10 | `planner_support.appendRejection`: path `functions[i].{field}[j]`, detail copied verbatim | done: rejection construction helper (strings are caller-owned; the planner copies detail text into caller storage per header contract) |
| P11 | `collective_synthesis.synthesize`: non-barrier dtype not native -> tsir_target_unfit; kind/exactness not natively supported -> tsir_collective_not_representable ("target lacks native collective exactness"); fabric kinds (fabric_reduce/broadcast/allreduce) consume colors in source order, exhaustion -> tsir_collective_not_representable ("target fabric color budget exhausted"); tree_shape always linear; group_size = min(max_group, tile on axis) when axis >= 0 and in range, else max_group | done |
| P12 | Exactness satisfaction: exact name match, or capability bit_exact_solo satisfies algorithm_exact and tolerance_bounded | done |
| P13 | `LoaderCapabilities`: fabric_streaming (default false), max_stream_chunk_bytes (default 0); `Options`: loader, emitter_digest (default zero), emitter_params_json (default above) | done |

## Reference interpreter: reference_interpreter.zig (762), reference_reduction.zig (497), reference_scalar.zig (113), reference_types.zig (18), reference_inputs.zig (26), reference_extended_ops.zig (841)

Locked numerical contract (Zig header): IEEE-754 round-to-nearest-even
for elementary ops, fp32 accumulation for every reduction regardless of
source dtype unless declared otherwise, left-fold unless
associative_allowed with a declared tree shape, deterministic
transcendentals (sollya-bounded minimax, libm NOT used), NaN/Inf
propagation as declared (default propagate). The interpreter is the
parity oracle: backends are compared against it, never against each
other. Unsupported paths return NotImplemented with a rejection
taxonomy, never a silent zero.

| Row | Zig behavior | C status |
| --- | --- | --- |
| R1 | `run` dispatch order: rejections present (semantic or realization) -> RejectedBySemantic; empty kernel; simple reduction; fused GEMV; RMSNorm; gather; identity; residual_add; gated; attention_scores; l2_normalize; conv1d_depthwise; linear_attention; else NotImplemented | done: full order preserved, extended ops via `tsir_try_extended_ops` after gather; linear_attention falls through exactly as Zig does (see R28) |
| R2 | `Result`: reference_hash = SHA-256 over the canonical byte image of all output buffers in declared order; outputs caller-owned; empty outputs when rejected | done: `TsirReferenceResult` with caller-owned buffers and `tsir_reference_free_result` |
| R3 | `freeResult` semantics | done |
| R4 | Empty kernel: exactly 1 function, 0 bindings, 0 reductions, 0 collectives, 0 inputs -> hash of empty input (e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855) | done, including the exact hash constant in tests |
| R5 | `reductionIdentityF32`: sum 0, product 1, min +inf, max -inf; compound softmax is not a scalar fold | done: four valid identities; C `TSIR_ERR_INVALID` matches the corrected retained Zig `InvalidReductionOp` error for softmax, replacing the former Zig trap. |
| R6 | `combineF32`: four valid scalar fold operators; compound softmax invalid | done: valid folds retained; typed invalid-operation error in both languages, with no fabricated value. |
| R7 | `readF32FromBytes`: f32 little-endian bits; f16 upcast; bf16 as high 16 bits; i32/u32 unreachable (never dispatched for integer bindings in Phase A) | done for f32/f16/bf16; i32/u32 -> TSIR_ERR_UNSUPPORTED visibly |
| R8 | `writeF32AsElem`: f32 bits; f16 downcast (round-to-nearest-even, subnormal/Inf/NaN per IEEE conversion); bf16 via RNE (see R9) | done: f16 conversion implemented with explicit bit manipulation, not compiler `_Float16`, so `-Wpedantic` stays clean |
| R9 | `f32ToBf16Rne`: NaN -> high 16 bits with quiet bit 0x40 set; else bias 0x7fff + lsb, wrapping add, shift 16 | done, bit-exact |
| R10 | `computeExpectedBytes`: product of shape dims times elem size; zero dim -> 0; overflow -> null | done: returns TSIR_ERR_OVERFLOW visibly; zero dim -> 0 |
| R11 | `countReadOnlyBindings`, `inputBytesForReadOnlyBinding` (inputs ordered by ascending binding index of read-only bindings; out-of-range or read-write binding -> null) | done |
| R12 | Simple reduction shape: 1 function, 2 bindings (exactly one read-only, one read-write), 1 reduction, 0 collectives, f32 accumulation, op in {sum, product, min, max}, dtypes in {f32, f16, bf16} both sides, target_binding = write index | done |
| R13 | Simple reduction associativity: strict_ordered -> linear; associative_allowed requires exactly 1 realization function, semantic_index 0, exactly 1 reduction node, semantic_index 0, else fall through; ring identical to linear on single PE | done |
| R14 | Rank 1: scalar output shape [1], axis 0; n == 0 -> identity (with the Zig input-length quirk: inputs empty, or exactly one empty input); linear/ring left-fold; binomial pairwise fold with odd leftover carried | done |
| R15 | Rank 2: output shape [non-reduced dim], axis 0 or 1, row-major flat index (axis 0: r*n + out; axis 1: out*n + r); binomial per-output scratch, same pairwise rule | done |
| R16 | Rank 3: output drops reduced axis, row-major surviving dims; binomial per-output scratch | done |
| R17 | Rank 4+: generic N-D odometer over non-reduced coords, row-major strides, output rank-1; binomial NOT supported at rank 4+ (falls through to NotImplemented) | done: generic path done for linear/ring; binomial at rank 4+ -> TSIR_ERR_UNSUPPORTED |
| R18 | `tryFusedGemv`: 1 function, 0 collectives, 3 bindings, 1 reduction, 2 axes; body op fused_gemv with exactly 3 binding roles (matrix, vector, output, distinct) and 2 axis roles (output, reduction, distinct, axes 0 and 1 in that row-major order); reduction sum, axis = reduction axis, target = output, f32 accumulation; associativity dispatch as R13; equal dtypes in {f32, f16, bf16}; shapes [M,K], [K], [M]; matrix/vector read-only, output read-write; inputs exactly 2 in read-only binding order; K == 0 -> zeros; linear/ring left-fold dot; binomial pairwise fold of products | done |
| R19 | `tryRmsNorm`: 1 function, 0 collectives, >= 3 bindings, 1 reduction, 2 axes; body op rms_norm with formula sum_squares_mean_epsilon_rsqrt_scale and intermediate_scalar target; inputs count == read-only binding count; exactly 3 binding roles (input, scale, output, distinct) and 2 axis roles (hidden 0, reduction 1, hidden_extent_axis == hidden axis); reduction sum over reduction axis, f32 accumulation, strict_ordered, propagate; input/scale read-only, output read-write; equal shapes [H], equal dtypes in {f32, f16, bf16}; computation sum_sq left-fold, mean = sum/H, inv_rms = 1/sqrt(mean + eps), out[d] = x * inv_rms * scale[d]; hidden == 0 -> zero-length output | done |
| R20 | RMSNorm epsilon resolution: literal_f32 requires empty path, no binding/offset, finite literal, cast f64->f32; uniform_field requires path splitting to field "eps", binding name matching, read-only binding, offset + 4 bytes within the uniform input, finite f32 value | done: path split implemented for the `uniform:<binding>.<field>` shape the Zig splitter accepts (see gaps.md for the exact splitter rule) |
| R21 | `tryGather`: 1 function, 0 collectives, 0 reductions, 3 bindings, 2 axes; body op gather with roles indices/table/output (distinct) and axes token 0, hidden 1; indices/output read-write rules (indices, table read-only; output read-write); indices elem u32; table/output equal dtypes in {f32, f16, bf16}; shapes [tokens], [vocab, hidden], [tokens, hidden]; inputs exactly 2 in read-only order; every index validated < vocab (out-of-range falls through to NotImplemented, not an error value); output is a byte copy of table rows | done: out-of-range index -> TSIR_ERR_UNSUPPORTED (fall-through equivalent), not a fabricated row |
| R22 | `tryIdentity` (extended ops) | done: `tsir_extended.c`, byte copy with shape/elem/input-length checks |
| R23 | `tryResidualAdd`: output[i] = summand_a[i] + summand_b[i] | done: `tsir_extended.c`, f32 left-fold-free elementwise add |
| R24 | `tryGated`: gelu (tanh approximation, Doppler MLP form), silu, sigmoid gated products | done: `tsir_extended.c` with the exact Zig clamp forms (-15/15) and tanh form constants; transcendental evaluation is double-precision rounded to f32, so bit parity against Zig is unestablished (gaps.md G-TRANSCENDENTAL-BIT); tests assert exact cases bit-exactly and the rest within tolerance |
| R25 | `tryAttentionScores`: fused softmax_stable attention body | done: `tsir_extended_ops.c`, single-Q decode shape only, causal as structural no-op, sliding-window masking, sentinel -1.0e30, left-fold f32 accumulation; exp via double-precision (see G-TRANSCENDENTAL-BIT) |
| R26 | `tryL2Normalize`: output[d] = input[d] / sqrt(sum input^2 + eps) | done: `tsir_extended_ops.c`; sqrt via double-precision (see G-TRANSCENDENTAL-BIT) |
| R27 | `tryConv1DDepthwise`: causal left-padded depthwise conv, kernel_size parameter, optional bias | done: `tsir_extended_ops.c`, including the has_bias/bias-role agreement rule |
| R28 | `tryLinearAttention`: gated DeltaNet state update and readout | done, with the Zig fall-through preserved: dispatch always falls through because Zig reads the read-write state through the read-only input helper (gaps.md G-LINEAR-DEAD-PATH); the state-update math itself is implemented as `tsir_linear_attention_step` (a_log pinned to 0.5, as in Zig) and tested directly |
| R29 | `TranscendentalTable`: exp/log/sin/cos/tan/tanh/rsqrt/recip ULP bounds (1,1,1,1,2,2,1,1) as declared metadata | done: constant table in `tsir_reference.h`; exp/tanh/sqrt are now also evaluated (double-precision, rounded to f32, in `tsir_transcendental.c`), not as Zig's declared minimax polynomials, so bit parity is unestablished; log/sin/cos/tan/rsqrt/recip have no C function |
| R30 | Interpreter error taxonomy: OutOfMemory, NotImplemented, RejectedBySemantic | done: TSIR_ERR_NOMEM, TSIR_ERR_UNSUPPORTED, TSIR_ERR_REJECTED |

## Emitters

Shared rule (all emitters): any realization-level rejection blocks
emission (`RejectedRealization`); function index out of range is an
error; semantic-aware emission checks the realization function's
target_descriptor_hash against the descriptor hash and fails on
mismatch. The low-level planner now computes the actual complete descriptor
hash through the shared target owner. Emitters compare the supplied identity
against that stored hash. Span variants preserve target name and metadata bytes,
including embedded NUL; exact Zig skeleton identity is tested.

| Row | Zig behavior | C status |
| --- | --- | --- |
| E1 | `emit_semantic_metadata.writeResidency`: `// residency.count = N` then per decision binding_index, class, axis, shards, fabric_color, chunk_bytes (optional values print as `none`), blank line | done: byte-exact writer |
| E2 | `writeTiles`: count then `// tiles.per_axis[i] = v`, blank line | done |
| E3 | `writeCollectives`: count then semantic_index, tree_shape, fabric_color, group_size per node, blank line | done |
| E4 | `writeReductions`: count then semantic_index, tree_shape per node, blank line | done |
| E5 | `writeOptionalU32/U64`: value or `none`, each with trailing newline | done |
| E6 | `writeHash`: 64 lowercase hex chars, no newline added by the writer itself | done |
| E7 | `writeQuoted`: double-quoted, escapes `\\`, `"`, `\n`, `\r`, `\t` only (other bytes pass through, including control bytes Zig's digest escaper would escape; the metadata writer is not the digest escaper) | done: restriction preserved |
| E8 | `emit_text_skeleton` contract header: `// {version_key} = 1`, `// target.name = {name}`, `// target.descriptor_hash = {hex}`, `// semantic_index = N`, `// pe_grid.width/height`, `// emitter_params_json = {quoted}`, blank line; then residency, tiles, collectives, reductions; then kernel body (semantic-aware) or body_comment + newline (realization-only) | done: header plus metadata plus realization-only body_comment byte-exact, and the semantic-aware path now appends the kernel body (E13) with no partial text on failure |
| E9 | DXIL skeleton spec: version_key `doe.tsir.dxil_skeleton.version`, body_comment `; tsir mechanical skeleton: DXIL module body is emitted by later lowering.` | done: realization-only emission |
| E10 | MSL skeleton spec: version_key `doe.tsir.msl_skeleton.version`, body_comment `// tsir mechanical skeleton: MSL kernel body is emitted by later lowering.` | done: realization-only emission |
| E11 | SPIR-V skeleton spec: version_key `doe.tsir.spir_v_skeleton.version`, body_comment `; tsir mechanical skeleton: SPIR-V module body is emitted by later lowering.` | done: realization-only emission |
| E12 | WebGPU emitter: realization-only calls emit the contract skeleton (its own header/metadata layout, same writers); semantic-aware calls emit executable WGSL for supported body families via kernel_body | done: realization-only skeleton plus WGSL bodies for fused_gemv, rms_norm, gather via `tsir_kernel_body.c` |
| E13 | `emit_kernel_body` (896 lines): Backend enum webgpu/csl/msl/dxil/spir_v; webgpu/msl/dxil/spir_v bodies support fused_gemv, rms_norm, gather and reject all other ops (including attention_scores, which is fixture-only) with UnsupportedKernelBody; CSL bodies support all 13 non-unknown ops (gated via emit_kernel_body_gated, attention via emit_kernel_body_attention, conv1d, l2_normalize, linear_attention, kv read/write, residual_add) and are build-gated on enable_csl | done (non-CSL scope): `tsir_kernel_body.c` (WGSL/MSL/DXIL/SPIR-V bodies for fused_gemv, rms_norm, gather; all other ops return TSIR_ERR_UNSUPPORTED; backend enum `TsirKernelBackend` in `tsir_emit.h`); CSL backend returns TSIR_ERR_UNSUPPORTED and stays quarantined, not built |
| E14 | `emit_kernel_context.Config` and defaults: var_prefix `tsir_`, no param defaults, no Gemma offset, kv full_per_pe, attention full_per_pe, tile_param pe-id source | done: struct plus defaults (as configuration data; the CSL bodies that consume it remain excluded per E13) |
| E15 | `emit_kernel_context` helpers: bindingForRole (first matching role, else MissingBindingRole), bindingForIndex (out of range -> InvalidBodyContract), requireElem (mismatch -> UnsupportedScalarKind), writeCslSqrtNr (f32 form vs upcast form), writeCslBufferArray, writeCslBufferPointer, writeCslExportSymbol | done: helpers implemented byte-exact for the CSL text fragments they produce; they are CSL text helpers, not a CSL emitter claim |
| E16 | `emit_dtype_routing`: cslElemName (f32/f16/bf16/i32/u32 verbatim), wgslElemName (bf16 maps to f16 spelling, rejected elsewhere), isSupportedComputeElem (f32/f16 only), requireSupportedComputeElem, writeWgslF16Enable (`enable f16;\n\n` only for f16) | done |
| E17 | `emit_csl_f16_pack` (48 lines): writeBufferArray, writeBufferPointer, writePackLoop, writeExportSymbol | done: text helpers (quarantined CSL text; see n/a note) |
| E18 | CSL emitter proper (`emit_csl.zig`, 237 lines): realization-only CSL-shaped contract text; semantic-aware executable PE bodies for supported families | n/a (quarantined): realization-only CSL contract shape is NOT reimplemented here, because CSL is excluded from default parity and the coordinator clarified CSL can be ignored. Mapped, not built. |
| E19 | Per-op CSL body files: emit_kernel_body_attention (443), emit_kernel_body_kv (245), emit_kernel_body_linear_attention (169), emit_kernel_body_gated (115), emit_kernel_body_conv1d (110), emit_kernel_body_l2_normalize (73) | n/a (quarantined): mapped, not built |
| E20 | `emit_webgpu` semantic-aware WGSL bodies (fused_gemv, rms_norm, gather) | done: `tsir_kernel_body.c` WGSL emitters, exact texts, literal and uniform epsilon forms |
| E21 | `emit_msl`/`emit_dxil`/`emit_spir_v` semantic-aware bodies (fused_gemv, rms_norm, gather, via kernel_body) | done: `tsir_kernel_body.c` MSL/DXIL/SPIR-V emitters, exact texts, including the SPIR-V gather ternary with 0.0 (not 0.0f) |
| E22 | Emitter code digests (`emitterCodeDigest`): SHA-256 over the emitter's own source text plus the shared skeleton/body/metadata sources it embeds | reference identity implemented for WebGPU/MSL/SPIR-V/DXIL by hashing actual retained emitter inputs; seven exact target/emitter comparisons against Zig pass. This is reference provenance, not a claim that C implementation source hashes equal Zig. |
| E23 | `emit_kernel_body` Config-driven live-path knobs (chunk_size_default 1024, hidden_size_default 1024, Gemma 1+weight offset, kv head_dim/max_seq_len/read_len defaults, slot-sharded kv, kv-axis-sharded attention, attention pe-id source) | n/a (quarantined): these live knobs are consumed only by retained CSL bodies, not by retained non-CSL bodies; no non-CSL migration gap is inferred |

## Tools: tools/ (751 lines)

Both tools now have standalone C executables built by `tsir-bootstrap-tools`.
The manifest uses the existing C parser/sema and a typed TSIR adapter, then shared
frontend/planner/digest owners. The oracle shares the reference interpreter and
bounded JSON reader. Both consume `tsir.h`; CSL stays excluded.

| Row | Zig behavior | C status |
| --- | --- | --- |
| T1 | `tsir_bootstrap_manifest_inputs`: emits schema-tool inputs for bootstrap manifest lowering fixtures; kernels fused_gemv (algorithm_exact, invariants reduction_order/accum_dtype), rms_norm (tolerance_bounded max_abs 1e-6), gather (bit_exact_solo); frontend version `frontend-bootstrap-pipeline-v1`, compiler version `doe-tsir-bootstrap-2026-04-24`, WSE3 stream chunk 4096, max WGSL 1 MiB | implemented for non-CSL bootstrap: actual WGSL pipeline and nine-entry output match retained Zig 0.15.2 byte for byte, including source mutations; explicit source-adapter limits in ../../TSIR_BOOTSTRAP.md. |
| T2 | `tsir_bootstrap_oracle`: CLI `--kernel <name> --inputs <path> [--semantic-tsir] [--realization-tsir]`; builds Phase A semantic shapes for fused_gemv/rms_norm/gather, runs reference interpreter, prints status/hash; 16 MiB input cap; NotImplemented/InvalidJson reported as `not_implemented` status, not a crash | implemented standalone CLI and library: 67 independent/differential cases, 211 tracked allocation failures; preserves invalid JSON versus execution/file failure boundaries. |

## Interpretation

The original 145-row mapping is a triage aid, not universal parity evidence.
T1/T2 and all four non-CSL reference emitter identities have executable
acceptance above. Registry-wide behavior, unrelated interpreter corners and
physical runtime qualification are not implied; CSL remains excluded.
