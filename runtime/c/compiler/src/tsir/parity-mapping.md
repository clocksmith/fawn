# TSIR C-versus-Zig parity mapping

Source tree (read-only, not edited): `~/workspace/fawn-tree`, head
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

## The registry gap, stated plainly

The task asked for a row-by-row mapping against a 103-row capability
registry, and for the known state that C provides 250 of 266 listed native
API names with 16 retained stubs, delegation-only paths, or no-ops.

Searches of this tree found no 103-row registry file and no 266-name
native API inventory file: `grep` over `runtime/c` and `runtime/zig`
for a capability registry, and over `docs/c-migration/` for a 250/266
inventory, returned no such table. The coordinator's prior summary says
the inventory file lives on the user's Mac at
`/Users/xyz/.local/state/deco/doe-zig-c-gap-20261026/summary.json`,
which is not accessible from this VM. That file was not consulted, and
no registry row is claimed here from memory.

Consequences, applied throughout this document:

1. This mapping maps TSIR's own observable surface instead: every public
   Zig type, function, and behavior unit in the 40 files, grouped by file.
2. The 103-row registry itself remains an open gap (row R0 below). The
   coordinator must supply the inventory file before a registry-row
   mapping can be written; nothing here substitutes for it.
3. Existing C behavior for TSIR is: none. `grep -rn Tsir|TSIR` over
   `runtime/c/compiler/src/*.c|*.h` returned nothing. The existing C
   WGSL compiler and SPIR-V text emitters are a different subsystem and
   are NOT counted as TSIR parity. Every TSIR row below therefore starts
   from "no C behavior" and is classified by what this run implements
   in `src/` versus what it leaves missing.

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

## R0: registry gap (unresolved, not papered over)

| Row | Zig behavior | Existing C behavior | This run |
| --- | --- | --- | --- |
| R0 | 103-row capability registry, C-versus-Zig parity per row | No registry file found in tree; no TSIR C code at all | missing: needs the Mac inventory file. This document maps the TSIR surface instead and does not claim registry coverage. |
| R0b | 266 listed native API names, 250 provided by C, 16 stubs/delegation/no-ops | Not verifiable from this tree (no inventory file found) | missing: carried forward as coordinator-supplied context only; no stub is reproduced or "fixed" here, and no C function in `src/` returns success for an unimplemented capability. |

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
| S31 | `AttentionScoresBody`: softmax_mode, head_dim, key_sequence_axis, scale_source, optional scale binding/offset/literal, has_softcap, causal_mode, optional sliding_window_size, optional query_seq_len | done: struct only; no interpreter/emitter for it (see X-rows) |
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
C uses `%.17g` with a shortest-round-trip correction only where needed:
see gaps.md for the known divergence risk on non-integral f64 values
(tolerance_epsilon, literal_f32). Integral and small decimal values used
by the tests match exactly.

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
`ir_query`, `layout_utils`), which is outside the TSIR directory and
has no C counterpart in this output directory. A faithful
`lowerIrToTsir` therefore cannot be written here without first porting
the WGSL IR; inventing a lookalike IR would be a stub, not parity.

| Row | Zig behavior | C status |
| --- | --- | --- |
| F1 | `lowerIrToTsir`: per-function bindings (only globals actually referenced by that function, with a bound @group/@binding), iteration axes, reductions, collectives, rejections, family hint, semantic body; contract_version 1, frontend_version pinned by caller, source_digest carried per function | missing: `tsir_frontend_lower` returns TSIR_ERR_UNSUPPORTED visibly. Gap, not a stub. |
| F2 | `frontend_types.scalarKindFromIr`: f32->f32, f16->f16, i32->i32, u32->u32, abstract_int->i32, abstract_float->f32, bool->u32, void->u32 | done: `tsir_ir_scalar_to_kind` over a C mirror enum of exactly those 8 IR scalar kinds; Zig test cases reproduced in tests |
| F3 | `frontend_types.FrontendError`: OutOfMemory only | done: mapped to TSIR_ERR_NOMEM |
| F4 | `frontend_body.inferSemanticBody` (178 lines): body inference from hint/axes/bindings/reductions | missing: returns TSIR_ERR_UNSUPPORTED |
| F5 | `frontend_collectives.collectCollectives` (246 lines) | missing: returns TSIR_ERR_UNSUPPORTED |
| F6 | `frontend_expr` (637 lines): literal/symbolic upper bounds, step, init bound, dispatch-guard scan, step-sign detection, dispatch-axis letter extraction | missing: returns TSIR_ERR_UNSUPPORTED |
| F7 | `frontend_rejections.recoverRejections` (122 lines): typed rejections for unrepresentable constructs, never placeholder values | missing: returns TSIR_ERR_UNSUPPORTED |
| F8 | `family_hint.infer` (183 lines): reductions present -> fused_gemv only when 2 axes, 1 sum reduction on axis 1, nontrivial bound string (dispatch./uniform:/override:/override@id:/const:), and one indexed load covering both axis locals; else reduction; axes present -> gather when any indirect buffer access, else elementwise; else unknown. Hints are tiebreakers only and must not change feasibility | partial: the pure shape predicate (reduction/fused_gemv/elementwise/unknown classification from axis/reduction/bound facts, without IR expression walking) is implemented as `tsir_family_hint_classify`; the IR-walking parts (indexed-access coverage, indirect-access detection) are missing and return the conservative fallback the Zig code itself uses when evidence is absent (reduction / elementwise), documented in gaps.md. This is the one row where "partial" needs care: the C function never claims IR evidence it did not examine. |
| F9 | `frontend_body`/`frontend_expr`/`frontend_collectives` internal helpers | missing, same reason as F1 |

## Planner: planner.zig (295), planner_support.zig (47), collective_synthesis.zig (144)

The planner consumes `targets.TargetDescriptor`. Only the correctness
fields the planner actually reads are mirrored in C as
`TsirTargetDescriptor` (see `tsir_planner.h`): pe_working_memory_bytes,
fabric_color_count, max_collective_group_size, native_numerical_modes
(f32/f16/bf16 names), native_collectives (kind_name, exactness_name),
runtime_sized_binding_policy (host_copied / fabric_streamed_with_loader
/ reject). The C descriptor is a plain caller-owned struct, not a claim
about the full Zig target descriptor.

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
| R1 | `run` dispatch order: rejections present (semantic or realization) -> RejectedBySemantic; empty kernel; simple reduction; fused GEMV; RMSNorm; gather; identity; residual_add; gated; attention_scores; l2_normalize; conv1d_depthwise; linear_attention; else NotImplemented | partial: dispatch order preserved for the implemented paths (empty, simple reduction, GEMV, RMSNorm, gather) followed by TSIR_ERR_UNSUPPORTED for the extended-op shapes. Rejection-first rule done. |
| R2 | `Result`: reference_hash = SHA-256 over the canonical byte image of all output buffers in declared order; outputs caller-owned; empty outputs when rejected | done: `TsirReferenceResult` with caller-owned buffers and `tsir_reference_free_result` |
| R3 | `freeResult` semantics | done |
| R4 | Empty kernel: exactly 1 function, 0 bindings, 0 reductions, 0 collectives, 0 inputs -> hash of empty input (e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855) | done, including the exact hash constant in tests |
| R5 | `reductionIdentityF32`: sum 0, product 1, min +inf, max -inf; softmax_stable unreachable (Zig trap) | partial: identities done for the four foldable ops; softmax_stable returns TSIR_ERR_INVALID visibly in C instead of trapping. Documented divergence: C refuses where Zig would trap; neither produces a value. |
| R6 | `combineF32`: sum +, product *, min/max IEEE min/max with NaN propagation per propagate contract; softmax_stable unreachable | done for the four ops; softmax_stable -> TSIR_ERR_INVALID |
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
| R22 | `tryIdentity` (extended ops) | missing: TSIR_ERR_UNSUPPORTED |
| R23 | `tryResidualAdd`: output[i] = summand_a[i] + summand_b[i] | missing: TSIR_ERR_UNSUPPORTED |
| R24 | `tryGated`: gelu (tanh approximation, Doppler MLP form), silu, sigmoid gated products | missing: TSIR_ERR_UNSUPPORTED. Deliberately not approximated with libm: the Zig oracle uses declared transcendental behavior and the gated path's exact polynomial is in the extended-ops file; a libm approximation would pass a smoke test while breaking bit parity. |
| R25 | `tryAttentionScores`: fused softmax_stable attention body | missing: TSIR_ERR_UNSUPPORTED |
| R26 | `tryL2Normalize`: output[d] = input[d] / sqrt(sum input^2 + eps) | missing: TSIR_ERR_UNSUPPORTED |
| R27 | `tryConv1DDepthwise`: causal left-padded depthwise conv, kernel_size parameter, optional bias | missing: TSIR_ERR_UNSUPPORTED |
| R28 | `tryLinearAttention`: gated DeltaNet state update and readout | missing: TSIR_ERR_UNSUPPORTED |
| R29 | `TranscendentalTable`: exp/log/sin/cos/tan/tanh/rsqrt/recip ULP bounds (1,1,1,1,2,2,1,1) as declared metadata | done: constant table in `tsir_reference.h`; the polynomial implementations themselves are missing (they are what R24-R28 need), so the table is metadata only and is labeled as such |
| R30 | Interpreter error taxonomy: OutOfMemory, NotImplemented, RejectedBySemantic | done: TSIR_ERR_NOMEM, TSIR_ERR_UNSUPPORTED, TSIR_ERR_REJECTED |

## Emitters

Shared rule (all emitters): any realization-level rejection blocks
emission (`RejectedRealization`); function index out of range is an
error; semantic-aware emission checks the realization function's
target_descriptor_hash against the descriptor hash and fails on
mismatch. The C descriptor hash is caller-supplied (the Zig
`targets.descriptorHash` lives outside TSIR); the C emitter compares the
supplied 32 bytes against the realization function's stored hash, which
is exactly the check the Zig code performs once the hash is computed.
The hash computation itself is missing (it belongs to the targets
module), and is listed in gaps.md.

| Row | Zig behavior | C status |
| --- | --- | --- |
| E1 | `emit_semantic_metadata.writeResidency`: `// residency.count = N` then per decision binding_index, class, axis, shards, fabric_color, chunk_bytes (optional values print as `none`), blank line | done: byte-exact writer |
| E2 | `writeTiles`: count then `// tiles.per_axis[i] = v`, blank line | done |
| E3 | `writeCollectives`: count then semantic_index, tree_shape, fabric_color, group_size per node, blank line | done |
| E4 | `writeReductions`: count then semantic_index, tree_shape per node, blank line | done |
| E5 | `writeOptionalU32/U64`: value or `none`, each with trailing newline | done |
| E6 | `writeHash`: 64 lowercase hex chars, no newline added by the writer itself | done |
| E7 | `writeQuoted`: double-quoted, escapes `\\`, `"`, `\n`, `\r`, `\t` only (other bytes pass through, including control bytes Zig's digest escaper would escape; the metadata writer is not the digest escaper) | done: restriction preserved |
| E8 | `emit_text_skeleton` contract header: `// {version_key} = 1`, `// target.name = {name}`, `// target.descriptor_hash = {hex}`, `// semantic_index = N`, `// pe_grid.width/height`, `// emitter_params_json = {quoted}`, blank line; then residency, tiles, collectives, reductions; then kernel body (semantic-aware) or body_comment + newline (realization-only) | partial: header plus metadata plus realization-only body_comment done byte-exact; the semantic-aware path's kernel-body step is missing (see E13), so `tsir_emit_semantic` returns TSIR_ERR_UNSUPPORTED rather than emitting a skeleton with a fake body |
| E9 | DXIL skeleton spec: version_key `doe.tsir.dxil_skeleton.version`, body_comment `; tsir mechanical skeleton: DXIL module body is emitted by later lowering.` | done: realization-only emission |
| E10 | MSL skeleton spec: version_key `doe.tsir.msl_skeleton.version`, body_comment `// tsir mechanical skeleton: MSL kernel body is emitted by later lowering.` | done: realization-only emission |
| E11 | SPIR-V skeleton spec: version_key `doe.tsir.spir_v_skeleton.version`, body_comment `; tsir mechanical skeleton: SPIR-V module body is emitted by later lowering.` | done: realization-only emission |
| E12 | WebGPU emitter: realization-only calls emit the contract skeleton (its own header/metadata layout, same writers); semantic-aware calls emit executable WGSL for supported body families via kernel_body | partial: realization-only skeleton done; semantic-aware WGSL body emission missing (see E13) |
| E13 | `emit_kernel_body` (896 lines): Backend enum webgpu/csl/msl/dxil/spir_v; webgpu/msl/dxil/spir_v bodies support fused_gemv, rms_norm, gather and reject all other ops (including attention_scores, which is fixture-only) with UnsupportedKernelBody; CSL bodies support all 13 non-unknown ops (gated via emit_kernel_body_gated, attention via emit_kernel_body_attention, conv1d, l2_normalize, linear_attention, kv read/write, residual_add) and are build-gated on enable_csl | missing: `tsir_emit_kernel_body` returns TSIR_ERR_UNSUPPORTED for every op. This is the largest single emitter gap. The per-op body files are listed individually below so the coordinator can schedule them. |
| E14 | `emit_kernel_context.Config` and defaults: var_prefix `tsir_`, no param defaults, no Gemma offset, kv full_per_pe, attention full_per_pe, tile_param pe-id source | done: struct plus defaults (as configuration data; the bodies that consume it are missing per E13) |
| E15 | `emit_kernel_context` helpers: bindingForRole (first matching role, else MissingBindingRole), bindingForIndex (out of range -> InvalidBodyContract), requireElem (mismatch -> UnsupportedScalarKind), writeCslSqrtNr (f32 form vs upcast form), writeCslBufferArray, writeCslBufferPointer, writeCslExportSymbol | done: helpers implemented byte-exact for the CSL text fragments they produce; they are CSL text helpers, not a CSL emitter claim |
| E16 | `emit_dtype_routing`: cslElemName (f32/f16/bf16/i32/u32 verbatim), wgslElemName (bf16 maps to f16 spelling, rejected elsewhere), isSupportedComputeElem (f32/f16 only), requireSupportedComputeElem, writeWgslF16Enable (`enable f16;\n\n` only for f16) | done |
| E17 | `emit_csl_f16_pack` (48 lines): writeBufferArray, writeBufferPointer, writePackLoop, writeExportSymbol | done: text helpers (quarantined CSL text; see n/a note) |
| E18 | CSL emitter proper (`emit_csl.zig`, 237 lines): realization-only CSL-shaped contract text; semantic-aware executable PE bodies for supported families | n/a (quarantined): realization-only CSL contract shape is NOT reimplemented here, because CSL is excluded from default parity and the coordinator clarified CSL can be ignored. Mapped, not built. |
| E19 | Per-op CSL body files: emit_kernel_body_attention (443), emit_kernel_body_kv (245), emit_kernel_body_linear_attention (169), emit_kernel_body_gated (115), emit_kernel_body_conv1d (110), emit_kernel_body_l2_normalize (73) | n/a (quarantined): mapped, not built |
| E20 | `emit_webgpu` semantic-aware WGSL bodies (fused_gemv, rms_norm, gather) | missing: TSIR_ERR_UNSUPPORTED |
| E21 | `emit_msl`/`emit_dxil`/`emit_spir_v` semantic-aware bodies (fused_gemv, rms_norm, gather, via kernel_body) | missing: TSIR_ERR_UNSUPPORTED |
| E22 | Emitter code digests (`emitterCodeDigest`): SHA-256 over the emitter's own source text plus the shared skeleton/body/metadata sources it embeds | missing: the C sources are not the Zig sources, so a C "emitter digest" equal to the Zig value cannot be produced by hashing C files. `tsir_emit` does not offer a fake digest. A coordinator decision is needed: either the digest is defined over the C emitter sources (new identity, breaks cross-implementation manifest comparison) or the Zig source bytes are carried as data (not done here). Listed in gaps.md. |
| E23 | `emit_kernel_body` Config-driven live-path knobs (chunk_size_default 1024, hidden_size_default 1024, Gemma 1+weight offset, kv head_dim/max_seq_len/read_len defaults, slot-sharded kv, kv-axis-sharded attention, attention pe-id source) | missing with E13: config fields exist (E14), behavior does not |

## Tools: tools/ (751 lines)

Both tools depend on the full Doe pipeline (WGSL parser, sema, IR
builder, targets, TSIR frontend/planner/emitters) and on bootstrap
fixture files under `runtime/zig/tests/tsir/`. Neither can be ported
faithfully inside the TSIR-only output directory.

| Row | Zig behavior | C status |
| --- | --- | --- |
| T1 | `tsir_bootstrap_manifest_inputs`: emits schema-tool inputs for bootstrap manifest lowering fixtures; kernels fused_gemv (algorithm_exact, invariants reduction_order/accum_dtype), rms_norm (tolerance_bounded max_abs 1e-6), gather (bit_exact_solo); frontend version `frontend-bootstrap-pipeline-v1`, compiler version `doe-tsir-bootstrap-2026-04-24`, WSE3 stream chunk 4096, max WGSL 1 MiB | missing: constants recorded here, no C tool built |
| T2 | `tsir_bootstrap_oracle`: CLI `--kernel <name> --inputs <path> [--semantic-tsir] [--realization-tsir]`; builds Phase A semantic shapes for fused_gemv/rms_norm/gather, runs reference interpreter, prints status/hash; 16 MiB input cap; NotImplemented/InvalidJson reported as `not_implemented` status, not a crash | missing: no C tool built. The C reference interpreter covers the same three kernel shapes (R18, R19, R21) as a library, so a future C oracle CLI is a thin argument/JSON layer over `tsir_reference_run`, not a new interpreter. JSON parsing is the missing piece, and it must not be hand-rolled into the library. |

## Summary counts

- Rows mapped: R0, R0b, S1-S47 (47), D1-D19 (19), F1-F9 (9),
  P1-P13 (13), R1-R30 (30), E1-E23 (23), T1-T2 (2): 145 rows.
- Done: 115 rows. Partial: 6 rows (F8, R1, R5, R6, E8, E12; see the
  table notes for exact subsets). Missing: 22 rows (F1, F4, F5, F6, F7,
  F9, R22-R28, E13, E20, E21, E22, E23, T1, T2, R0, R0b).
  Quarantined (mapped, not built): 2 rows (E18, E19) plus the CSL parts
  of E13, which are text helpers only where implemented.
- File-level status is in gaps.md. No row above is marked done on the
  basis of an existing C subsystem, a stub, or a related-but-different
  implementation.
