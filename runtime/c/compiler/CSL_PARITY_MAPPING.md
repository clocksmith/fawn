# CSL C port, coordinator parity mapping

Label: CSL C port, untested / not run, parity not verified.
Source: clocksmith/fawn main d25e075341219fc398dedb9286c2669a1b966221, read from ~/workspace/fawn-current.
Output: ~/workspace/doe-c-work/csl-port/src/ (78 files, 9,827 lines total, all under 999 lines per file, SPDX MIT in first three lines, header guards).
Nothing was pushed to GitHub, nothing was committed, nothing was written to ~/workspace/fawn-tree.
Do NOT claim full Zig/C parity. "Done" below means code written, not tested or parity-verified.
"Partial" means a named subset only, a pattern template, or a descriptor form, not the full Zig template text.
"Missing" means no C entry point at all. Missing 0 as files does not mean parity.

Documented seam, all three workers: there is no C WGSL IR module in scope.
Kernel classification, layout, and PE emission take descriptors, not a Zig ir.Module:
emit/csl uses CslKernelDesc (pattern plus counts, globals, dims, flags, shapes the Zig
classifier derives from IR); TSIR CSL uses TsirCslKernelDesc (op, bindings, config,
body params). A future C IR port can fill descriptors from real IR without changing emitters.
Anything needing real IR, embedded WGSL fixtures, JSON manifest parsing, running cslc
or a driver, or GPU/Cerebras execution returns CSL_ERR_UNSUPPORTED visibly, never a silent stub.

Shared infrastructure (coordinator, not a worker): src/csl_error.h (CSL_ERR_*),
src/csl_text.h/.c (CslBuf growable writer, 83 lines total). Workers reused it, did not overwrite it.
Prior local work at ~/workspace/doe-c-work/csl/ (12 files, 859 lines, pre-NO-CSL) was reviewed
and reused only where correct against d25e075 Zig sources; its old parity-mapping.md claimed
many more files than exist in its src/, and those claims were not trusted. The new port was
verified against d25e075 Zig sources. Do not overwrite ~/workspace/doe-c-work/csl/; this
csl-port/ directory is the new, separate deliverable.

## Totals

- emit/csl: 37 Zig files, 13,508 lines. C: 49 csl_* files (non-spatial), 2,830 lines. Status: done 7, partial 30, missing 0.
- spatial/csl: 8 Zig files, 2,525 lines. C: 17 csl_spatial_* files, 3,863 lines. Status: done 3, partial 5, missing 0.
- TSIR CSL-only: 7 Zig files, 1,392 lines. C: 12 tsir_csl_* files, 3,134 lines. Status: done 6 (written, untested), partial 1, missing 0.
- All CSL in scope: 52 Zig files, 17,425 lines. C: 78 files, 9,827 lines, plus 3 UNRUN test drivers (597 lines).
- Explicitly excluded from this port, correctly: TSIR emit_csl_f16_pack.zig (48 lines, already done in tsir_emit.c) and TSIR emit_kernel_body.zig (896 lines, done non-CSL only).

## emit/csl, 37 files

| Zig file (lines) | Status | C file(s) | Known divergences |
| --- | --- | --- | --- |
| csl_compile_sections.zig (37) | done | csl_compile_sections.c/.h | Marker buffer 256 bytes, not Zig 128; same separator strings and body extraction. |
| csl_spec.zig (244) | done | csl_spec.c/.h | Constants, scalar names, Q4K/Q8_0 validation, element counts, structural validateOutput copied exactly. Direction/arch name helpers are additions, not Zig functions. MAX_RECT_DIM 512 (also used by spatial). |
| csl_storage_emit.zig (90) | done | csl_storage_emit.c/.h | Buffer/pointer/export text exact; ir.Module replaced by caller-supplied global list, same bound-storage-only filter and index-with-fallback. |
| csl_text_buffer.zig (54) | partial | csl_text_buffer.c/.h | Thin wrapper over growable CslBuf. Zig is fixed-size and fails closed without advancing on overflow; CslBuf grows (to 1 GiB) before CSL_ERR_TOO_LARGE. JSON escaping escapes every control byte as \u00XX; Zig writes non-\n\r\t controls raw. Valid JSON either way, bytes differ at those edges. |
| emit_csl.zig (21) | partial | csl_emit_core.c/.h (csl_emit) | Facade only, descriptor form, not ir.Module. MAX_OUTPUT 512 KiB preserved. |
| emit_csl_attention.zig (601) | partial | csl_kernels_b.c/.h | Streaming, decode, tiled PE text from descriptors, Q/K/V, sliding-window, decode-position markers validator requires. IR-derived buffer indices and per-variant body detail not ported; bodies are pattern templates, not full Zig template text. |
| emit_csl_classify.zig (800) | partial | csl_classify.c/.h, csl_kernel.c (contract) | Pattern enum, info fields as descriptor fields, patternContractValid rules copied exactly. IR walk not ported; csl_classify_ir returns CSL_ERR_UNSUPPORTED. |
| emit_csl_core.zig (168) | partial | csl_emit_core.c/.h | Two-section emission, contract check, layout/PE dispatch, structural validation, MAX_OUTPUT 512 KiB. Per-pattern validation not run inside core; dense_gemv and semantic reachable through facade although Zig core does not dispatch them (noted addition). |
| emit_csl_dense_gemv.zig (120) | done | csl_layout.c (layout), csl_kernels_a.c (PE) | Layout and PE text reproduced exactly from Zig (params, memcpy and collectives_2d imports, f16 activation/weight, f32 partial/output, widen loop, row loop, reduce_fadds, single-row completion guard). |
| emit_csl_dequant.zig (137) | partial | csl_kernels_a.c/.h | Dequant PE text, Q4K constants (QK_K 256, block bytes 144). IR-derived quant buffer shape is descriptor input. |
| emit_csl_elementwise.zig (170) | partial | csl_kernels_a.c/.h | Elementwise PE text, @range loop, no fabric DSDs. IR-derived operator body is descriptor-selected op name, recorded not expanded to full WGSL body. |
| emit_csl_exec_v1.zig (989) | partial | csl_exec_v1.c/.h | Op table (op to pattern, phase permission), paired-gate set exactly (silu_gated, sigmoid_gated, gelu_gated, o_gate), phase parsing, step-to-kernel/launch lowering over caller-parsed steps. JSON manifest parsing not ported, CSL_ERR_UNSUPPORTED. LayerPattern/numKvSharedLayers/sliding-window metadata lowering from manifest not ported. WGSL-filename-to-op mapping not ported as separate table; only op-name mapping via single op table. |
| emit_csl_fused.zig (264) | partial | csl_kernels_b.c/.h | Fused GEMV-dequant PE text, dequant marker, c2d params. IR-derived fusion detail is descriptor input. |
| emit_csl_fused_ffn.zig (139) | partial | csl_kernels_b.c/.h | Fused FFN PE text, silu/gelu, c2d_params, reduce_fadds markers. Full gate/up reduction body is template, not Zig text. |
| emit_csl_gather.zig (125) | partial | csl_kernels_a.c/.h | Gather PE text, indices/table/output markers, (rows_per_pe + tokens_per_chunk) * hidden_per_pe * 4 <= 48 KiB shape as descriptor fields. IR-derived shard mapping is descriptor input. |
| emit_csl_host.zig (656) | partial | csl_host.c/.h | WSE-3 constants, ModelConfig, SRAM estimates with exact Zig formulas, effective KV layer count with alias de-duplication, rope num_pairs = head_dim * partial_rotary_factor / 2, compilation manifest JSON, Python runner scaffold with markers Zig tests check (load_and_stage_weights, memcpy_h2d, init_state, STATE_BUFFERS). Full Python runner body (per-launch code generation) is compact equivalent, not Zig text. |
| emit_csl_host_compile_rewrite.zig (86) | done | csl_host_compile.c/.h | f16 compile-source rewrite (f32 to f16 on identifier boundaries, then exact Zig replace-all list) and fused-GEMV f32 reduction restore (exact replace list). |
| emit_csl_host_compile_source.zig (895) | partial | csl_host_compile.c/.h | Pattern-source table records exactly which patterns Zig has an embedded WGSL fixture for; sections come from descriptors via core, with f16 rewrite / fused restore. Embedded WGSL fixture sources and WGSL-to-IR analysis not ported. Correction: Zig patternSource returns NULL for dense_gemv; dense sections come from separate emitDenseGemvSections path, not patternSource. C table fixed accordingly, dense reached via core, emitted sections unchanged. |
| emit_csl_host_plan.zig (757) | partial | csl_host_plan.c/.h | CompileTarget/BindingMetadata/CslcPlan types, discovery labels, makeCslcPlan, compile-target metadata and params JSON, host-plan artifact JSON with Zig field names, structural validation (schema 3, wse3, explicit_host_plan). Full JSON-schema validation not ported. |
| emit_csl_host_runtime.zig (837) | partial | csl_host_runtime.c/.h | WeightMapping/StateBuffer/RuntimeConfig types, runtime-config JSON with Zig field names, Python runner scaffold (compact equivalent) with markers (load_and_stage_weights, memcpy_h2d, init_state, STATE_BUFFERS, decode_loop, position, sliding_window, EOS_TOKEN_ID, safe_run, TimeoutError, SRAM_OVERFLOW, sram_pre_check). Per-launch code generation and RDRR parsing not ported. |
| emit_csl_ir_walk.zig (889) | partial | csl_ir_walk.c/.h | Barrier names, barrier predicates over descriptor expression kinds, indent/integer writers, lane-mode builtin shim text, uniform-global-base predicate. Full IR statement/expression walker returns CSL_ERR_UNSUPPORTED. |
| emit_csl_kv_cache.zig (169) | partial | csl_kernels_b.c/.h | KV write/read PE text, cache/kv_ markers, max_seq_len/slots_per_pe params. |
| emit_csl_layout.zig (911) | partial | csl_layout.c/.h | Layout text for every core pattern. Element-wise, reduction, dense GEMV follow Zig shapes closely; other patterns share rectangle + memcpy + tile-code + export skeleton with pattern-specific params as descriptor inputs. Not byte-identical for matmul/attention/kv patterns. Distributed-reduction layout in csl_kernels_b.c. |
| emit_csl_linear_attn.zig (130) | partial | csl_kernels_b.c/.h | Linear-attention PE text, output[d] = sum_k(Q[d] * K[k,d] * V[k,d]) * scale formula in comments, Q*K*V product loop. |
| emit_csl_maps.zig (160) | done | csl_maps.c/.h | Unary, binary, assign operator text, CSL math builtin table (13 entries), inline-expansion set (11 names), unsupported builtin set (textures, atomics, derivatives, subgroups) copied exactly. Zig logical operators and/or preserved. |
| emit_csl_matmul.zig (191) | partial | csl_kernels_a.c/.h | Tiled matmul PE text, accum marker, collectives_2d import. SUMMA broadcast loop detail is template, not Zig text. |
| emit_csl_matmul_q4k.zig (343) | partial | csl_kernels_a.c/.h | Q4K matmul PE text, QK_K_BLOCK_BYTES and dequant_b_tile markers, B_ptr as [*]u8 byte stream (validator forbids [*]f32 here), Q4_K_M constants 144/8/32/256. |
| emit_csl_mem_plan.zig (612) | partial | csl_mem_plan.c/.h | Residency modes, buffer kinds/placements, deriveGrid scan with exact betterGrid preference, plan JSON with Zig field names/order. Not ported: planForGrid deriving totals from ModelConfig + HostPlan and auto-choosing full_resident vs layer_streaming; caller supplies buffer totals and chooses mode, stream stages caller-supplied (empty by default). |
| emit_csl_reduce_dist.zig (275) | partial | csl_kernels_b.c/.h | Distributed reduction PE text, fabin/fabout DSD markers, east-west chain layout comment. Fabric queue/color wiring detail is template. |
| emit_csl_reduction.zig (157) | partial | csl_kernels_a.c/.h | Reduction PE text, // Workgroup shared -> PE-local marker for non-distributed path, reduce_color param. Lane-preserving pre-barrier loop lowering not ported. |
| emit_csl_rmsnorm_pack.zig (100) | done | csl_host_compile.c/.h | f16 output-pack rewrite, CHUNK_WORDS 128, CHUNK_COUNT 21, exact chunk declaration/pointer/pack-loop/layout-export/symbol-export insertions, default extent hidden_size, InvalidIr when needle absent, OutputTooLarge on overflow. |
| emit_csl_rope.zig (126) | partial | csl_kernels_a.c/.h | RoPE PE text, cos/sin markers, paired-dimension rotation. Interleaved vs rotate-half selection is not a descriptor field; emitted loop is interleaved pair form. |
| emit_csl_sample.zig (84) | partial | csl_kernels_a.c/.h | Sample PE text, logit marker, per-PE local argmax. Host-side sampling parity out of scope, as in Zig comment. |
| emit_csl_semantic_ops.zig (715) | partial | csl_semantic.c/.h | Semantic pattern set (rms_norm, residual_add, gelu/silu/sigmoid_gated, conv1d_depthwise, l2_normalize, linear_attention, attention_prefill_kv_axis_sharded), RMS epsilon 0.000001, layout/PE text per pattern with semantic formulas. TSIR kernel-body templates this Zig file imports are not ported here (TSIR worker scope); PE bodies are descriptor templates, not TSIR bodies. |
| emit_csl_simulator.zig (649) | partial | csl_simulator.c/.h | Simulator plan/result JSON with Zig field names/enums/constants, launcher script text failing closed on missing driver, structural validation. Driver execution not performed by library, CSL_ERR_UNSUPPORTED. Plan-to-typed-contract parsing beyond structural checks not ported. |
| emit_csl_toolchain.zig (184) | partial | csl_toolchain.c/.h | Makefile emission (cslc header, fabric dims, per-target rules, --memcpy --channels=1, all/clean targets, troubleshooting notes), compile-plan/simulator-plan JSON wrappers via host-plan/simulator emitters. Makefile text is structural equivalent, not byte-identical to Zig. |
| emit_csl_validate.zig (623) | partial | csl_validate.c/.h | Pattern validation (base spec check, SDK 2.10 compatibility including fabout/fabin output_queue/input_queue and no-fabric_color rules, per-pattern substring rules copied from Zig), toolchain config from environment value (empty fails, PATH sentinel case-insensitive, otherwise explicit path; env var DOE_WGSL_CSLC as in Zig). Running cslc --version not done by library, CSL_ERR_UNSUPPORTED for non-empty path, CSL_ERR_INVALID for empty. |

## spatial/csl, 8 files

| Zig file (lines) | Status | C file(s) | Known divergences |
| --- | --- | --- | --- |
| csl_bundle_emitter.zig (85) | partial | csl_spatial_bundle.c/.h (130/69) | Exact //--- layout.csl --- / //--- pe_program.csl --- split (first markers, pe marker strictly after layout marker end, trim \n\r space, both sides non-empty), --wgsl/--out-dir lookup, usage text, path joining. WGSL-to-CSL translation returns CSL_ERR_UNSUPPORTED; file writing/directory creation are caller job. |
| csl_dense_gemv_host_plan.zig (63) | done | csl_spatial_dense_gemv.c/.h (92/48) | TARGET_IN_PER_PE 32, width/height/in_dim_per_pe arithmetic (ceil div, even ceil div, clamps), 3 binding rows. CSL_SPATIAL_MAX_RECT_DIM pinned to confirmed Zig value 512; functions still take max_rect_dim explicitly so arithmetic stays testable. |
| csl_host_plan_bindings.zig (173) | done | csl_spatial_bindings.c/.h (166/64) | All 9 tables plus dense table via dense module, pattern dispatch including reduction alias to rmsnorm and three gated aliases, f16 lane rerouting rule. Unknown pattern returns found=0, not error, matching Zig null return. Dispatch table-driven (S1 below). BindingShape.kind uniformly "csl_array" default in these tables, omitted, loses nothing (verified against emit_csl_host_plan.zig:53-74). |
| csl_host_plan_materialize.zig (297) | partial | csl_spatial_materialize.c/.h (179/112) | Capacity constants, compile pattern mapping (prefill_q4k_gemv to fused_gemv_dequant), compile element mapping (dense_gemv/sample force f32), reserved dirs (compiled, driver-logs), kernel name test, stale-dir decision, default driver path rule, compile-target to targets-descriptor conversion (base kernel falls back to kernel name, phase NULL when absent), pe/layout/targets metadata emission into caller buffer. Not ported: host plan, memory plan, runtime config, simulator plan, launcher emission (each CSL_ERR_UNSUPPORTED); compile source generation itself is out of scope. |
| csl_host_plan_tool.zig (828) | partial | csl_spatial_host_plan_tool.c/.h (456/168), csl_spatial_host_plan_tool_params.c (723) | Argument parsing/validation, usage text, phase specialized kernel list and prefill/decode expansion, lm_head detection, dense GEMV hidden tile expansion (32-wide tiles, tile name <name>_width_tile_x<start>_w<count>), compile parameters for every pattern branch in Zig (gather, prefill_q4k_gemv, tiled_matmul incl. tiled_31b, dense_gemv, rms_norm/reduction, residual/gelu/element_wise, gated set, l2_normalize, conv1d_depthwise, linear_attention with 2-way state sharding, fused_gemv_dequant, kv_write, attention_tiled, attention_prefill_kv_axis_sharded, attention_decode, sample, rope incl. mrope sum check), maxModelFeatureDim, state buffer kind mapping. Not ported: execution JSON lowering (manifest and steps modes), host plan/memory/runtime/simulator/launcher emission, cslc plan construction (each CSL_ERR_UNSUPPORTED). Split into two files to stay under 999 lines. |
| csl_host_plan_tool_config.zig (241) | partial | csl_spatial_host_plan_tool_config.c/.h (159/115) | ModelConfig struct (Zig optionals as value plus has_* flags), quant format mapping (f16/q4k/q8_0), weight dtype mapping (f16/u8_q4k/u8_q8), activation dtype admission (f32/f16/absent) and scalar mapping, SHA-256 check (exactly 64 lowercase hex chars), pe range check (end > start), weight shape check (non-empty, every dim at least 1), typed weight mapping validator. Not ported: three JSON parse entry points (bundle model config, bundle activation dtype, bundle weight mappings), each CSL_ERR_UNSUPPORTED. JSON defaults from Zig (ffn_expansion_factor 4, ffn_matrix_count 3, partial_rotary_factor 1.0) documented in header but are the unported parser's job. |
| csl_pe_program_metadata.zig (610) | done | csl_spatial_pe_metadata.c/.h (972/103) | All four pe_program declaration shapes (typed var, @zeros var with duplicate-name suppression, pointer var [*]elem = &backing, @export_symbol), param/const lines with defaults (param without default not recorded), layout @export_name lines with parenthesis-depth type scanning and mutable flag, device_function/device_variable classification, export resolution (pointer to backing to declaration), three JSON emitters with byte-for-byte layout matching Zig writers (field order, commas, trailing newlines, phase: null handling). Parsed strings borrow source buffer. |
| csl_sim_runner.zig (228) | partial | csl_spatial_sim_runner.c/.h (201/106) | Usage text, argument parsing (--plan, --driver-executable, --result-json, --help), driver resolution order (explicit flag, then DOE_CSL_SIM_EXECUTABLE, then fail closed), artifact path resolution against plan directory, default result path (<trace>.result.json), termination-to-result status mapping (exited 0 succeeds, anything else fails, only exited carries exit code), trace existence check backing trace_produced. Not ported: simulator plan parsing/validation and result artifact emission (CSL_ERR_UNSUPPORTED), driver execution itself deliberately CSL_ERR_UNSUPPORTED, never spawns subprocess from library code. |

Resolved constants, verified against fresh clone at d25e075 (spatial worker, diff of all 8 local Zig files vs GitHub-fetched copies produced no output):
- csl_spec.MAX_RECT_DIM = 512 (csl_spec.zig:45), pinned CSL_SPATIAL_MAX_RECT_DIM.
- exec_v1.PREFILL_Q4K_GEMV_PATTERN = "prefill_q4k_gemv" (emit_csl_exec_v1.zig:41), earlier assumption correct.
- SIMULATOR_DRIVER_ENV_VAR = "DOE_CSL_SIM_EXECUTABLE" (csl_spec.zig:77), earlier literal correct.
- BindingMetadata mirror matches emit_csl_host_plan.zig:53-74 exactly.

## TSIR CSL-only, 7 files

| Zig file (lines) | Status | C file(s) | Known divergences |
| --- | --- | --- | --- |
| emit_csl.zig (237) | partial | tsir_csl_emit.c/.h (359/97) | Skeleton and semantic layout/pe_program written; digest always CSL_ERR_UNSUPPORTED (hashing C sources would give different digest); descriptor-hash comparison and function-index range checks caller-owned (no stored hash or function list in descriptor seam); semantic metadata blocks must be supplied pre-rendered, NULL meaning only empty count block. |
| emit_kernel_body_attention.zig (443) | done (written, untested) | tsir_csl_kernel_attention.c (737) | full_per_pe and kv_axis_sharded, single-Q and multi-Q, tile_param, layout_coordinates. |
| emit_kernel_body_conv1d.zig (110) | done (written, untested) | tsir_csl_kernel_conv1d.c (215) | With/without bias, causal left-only pad. |
| emit_kernel_body_gated.zig (115) | done (written, untested) | tsir_csl_kernel_gated.c (223) | gelu, silu, sigmoid with per-kind helper names. |
| emit_kernel_body_kv.zig (245) | done for slot_sharded only (written, untested) | tsir_csl_kernel_kv.c (415) | full_per_pe intentionally missing, CSL_ERR_UNSUPPORTED, that path lives in excluded emit_kernel_body.zig. |
| emit_kernel_body_l2_normalize.zig (73) | done (written, untested) | tsir_csl_kernel_l2.c (159) | Two-pass, f32 and f16 sqrt_nr forms. |
| emit_kernel_body_linear_attention.zig (169) | done (written, untested) | tsir_csl_kernel_linear_attention.c (326) | Shared-norm only; per_head and dt_bias return CSL_ERR_INVALID; no multi-Q entry point, matching Zig. |
| (shared) | - | tsir_csl_common.c/.h (243/293), tsir_csl_kernel_body.c/.h (30/37) | Shared compute tail, imports, buffer/export/sqrt_nr helpers, KV header, attention mask helper, single CSL dispatch. |

## Simplifications taken, by worker

Emit/csl (full list with proofs in emit-mapping.md):
1. One emit-helper header csl_emit_common: PUT/PUTF/PUTI, JSON string, PE preamble, scalar selector.
2. One JSON-string writer, not six.
3. One PE preamble, not two (kernels_a/kernels_b).
4. One layout skeleton for 13 patterns; element-wise, reduction, dense GEMV keep separate bodies (Zig distinct shapes).
5. One attention core (attn_common/attn_tail) shared by streaming, decode, tiled, linear attention; only comments, decode/sliding declarations, score cache, loop bodies differ.
6. One core dispatch chain (layout/PE dispatch kernels_a to kernels_b to semantic fallback).
7. One contract switch over CslKernelDesc fields, rules copied exactly from Zig patternContractValid.
8. One validate rule engine (REQ/BAD helpers plus one SDK 2.10 checker); rules unchanged.
9. One exec op table driving pattern lookup and phase permission; no second filename table.
10. Wrapper delegation, not duplicated JSON: toolchain compile-plan delegates to host-plan, simulator-plan wrapper delegates to simulator, host-compile pack delegates to extent-parameterised pack function.
11. Dead paths removed, provably dead in this port not in Zig: csl_desc_str (no-op fallback, zero callers, no Zig counterpart, deleted from csl_kernel.h/.c); csl_classify_from_ir (duplicate of IR-classification stub csl_classify_ir, zero callers, deleted, stub remains CSL_ERR_UNSUPPORTED). Neither changes emitted text or any Zig-mapped public API.
12. Near-duplicate nm helper replaced by inline ternary.
Deliberately NOT taken: KV write vs read collapse (buffer identity/copy direction are validator-visible markers); tiled vs Q4K matmul collapse (different B-operand types, constants, validator rule); removing tiled_matmul_q4k_dequant_b from core dispatch (Zig core dispatches it even though classifier comment says classifier never emits it yet); generic JSON object writer (each JSON keeps Zig field names/order, only string escaping shared); no simplification papers over a missing case.

Spatial/csl (full list in spatial-mapping.md, section Simplifications):
S1. Bindings dispatch table-driven (csl_spatial_bindings.c, 166 lines, was 159 with if/else chain): static {pattern, table, count, direct} table plus dense special case. Each pattern maps to exactly the table Zig chain selected, aliases share one table, no pattern twice with different tables. f16 reroute and unknown-pattern behavior unchanged.
S2. Identical tool parameter branches merged (csl_spatial_host_plan_tool_params.c, 723 lines): Zig two byte-identical branches (residual/gelu/element_wise, and gated set) are one branch; shared append_row_params covers rms_norm/reduction, that merged family, l2_normalize (same three-parameter shape, differing only in third name/value). Names, order, values unchanged.
S3. Shared target shell (csl_spatial_host_plan_tool.c, 456 lines): normal/phase targets and dense tile targets share init_target_shell for five duplicated assignments, same order, first-error precedence unchanged. Dead code removed in same pass: unused ParamList typedef, unused macros in tool file, placeholder macro in params file.
S4. Shared helpers already in place, now listed: append_param (with Zig @max(1, value) clamp) across parameter branches; ceil_div_u32 across dense/tool arithmetic; one copy-plus-reroute loop across lane bindings; one JSON string writer across three pe metadata emitters; compileTargetMetadata collapsed (one-line forward in Zig); writeFile/writeExecutableFile collapsed into one caller-responsibility rule (only Zig difference was chmod, belongs to layer that actually creates files); sim runner four makeResult arms collapsed into one outcome conditional; BindingShape.kind omitted (uniformly "csl_array" default, verified).
S5. Considered and rejected, left faithful: collapsing four pe metadata JSON emitters into one generic emitter (callbacks, indirection, field sets genuinely differ); shared argv parser across bundle/tool/sim (different semantics: first-match lookup vs output/bundle exclusivity vs fail-on-unknown); collapsing remaining tool parameter branches (gather, tiled, fused GEMV, attention variants, rope), formulas genuinely differ.
S6. Fidelity additions alongside, not simplifications: csl_spatial_materialize_target_descriptor ports Zig target-to-descriptor conversion (base kernel falls back to kernel name, absent phase becomes NULL); csl_spatial_sim_trace_exists ports Zig traceExists check behind trace_produced, which first pass had dropped.
Scope boundary: cross-emitter sharing across host plan, layout, mem plan, simulator, kernel-body emitters was NOT attempted by spatial worker, because those modules are owned by other workers and outside its scope. No file outside spatial set was touched for simplification.

TSIR CSL (full list in tsir-mapping.md, Simplifications section):
1. Shared compute tail tsir_csl_write_compute_tail (unblock, close compute, blank line, comptime block, one export per buffer in caller order, @export_symbol(compute)). All 7 bodies plus both attention paths and both KV bodies pass only buffer-name list; attention duplicate emit_exports helper removed. Export order verified per emitter by grep after refactor.
2. Shared imports tsir_csl_write_sys_mod and tsir_csl_write_math_import replace copied sys_mod/math lines in every emitter.
3. Gated silu/sigmoid collapse: Zig silu/sigmoid bodies identical except final return line, share one clamp block (var z = -x, plus/minus 15 clamps) with only return expression selected by kind. GELU untouched, body genuinely differs.
4. Shared emit_csl layout/pe_program header: write_tile_loop and write_pe_program_header replace duplicated skeleton/semantic tile loops and marker/contract/metadata/pe-param blocks.
5. Removed redundant NULL check in KV resolve_slot_sharded: tsir_csl_resolve_config already returns CSL_ERR_INVALID for NULL descriptor, same result.
6. Already shared before this pass: buffer array/pointer/export/sqrt_nr helpers, KV header shared by write/read, attention mask helper shared by both paths, single CSL dispatch, data-driven conv1d bias handling.
Rejected: merging full_per_pe and kv_axis_sharded attention, merging KV write/read beyond header, collapsing all bodies into one table-driven emitter (differences are logic, not just data); unifying scale/eps float formatting with Zig {e} (would change emitted bytes); removing inherited Zig quirks (KV i16 pe_id, sharded 8-space mask indent, hardcoded f32 layout export).
Dead-path audit: no Zig branch in scope found dead, none removed. Kept: attention query_seq_len validation on full_per_pe (validated in Zig even though unused there), non-attention layout case (now is_attention flag), KV slot-sharded absence of read_start/read_len (deliberate host-side slicing in Zig), defensive UNSUPPORTED returns for out-of-range enums and dispatch misuse.
Incident: first KV tail replacement swapped write/read export lists; syntax check caught it (wrong names undeclared); fixed by function (write: key_proj/val_proj/key_cache/val_cache/position; read: key_cache/val_cache/key_output/val_output), export order verified by grep, syntax check re-run clean. Recorded in tsir-mapping.md; behavioral parity tests must still be run before trusting refactored output.

## Worker mapping files (source detail, retained)

- emit-mapping.md (253 lines), emit-tests.md (59 lines)
- spatial-mapping.md (216 lines), spatial-tests.md (257 lines)
- tsir-mapping.md (324 lines), tsir-tests.md (215 lines)
These are the per-worker detail behind this coordinator mapping. Where a worker mapping and this coordinator mapping disagree, the worker mapping is the more detailed source for its directory, and the disagreement should be treated as a gap to resolve, not silently resolved here.
