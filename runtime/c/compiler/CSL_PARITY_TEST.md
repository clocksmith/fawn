# CSL C port, parity tests (definitions, NOT RUN)

Label: CSL C port, untested / not run, parity not verified.
All tests below are definitions. None was run as a behavioral test in this pass.
The only checks that ran were worker-reported Linux gcc syntax/compile checks (parsing only, see test-results.md).
A test passes only when the C emitter, given the same descriptor/inputs as the Zig emitter,
produces the stated output, or the stated visible error, with no hidden difference.

General method, for every file:
1. Take a Zig fixture / descriptor pair from the Zig source at d25e075 (or, where Zig
   derives values from IR, a descriptor filled with exactly those derived values, recorded
   in the test input).
2. Run the Zig emitter (or capture its expected text from the Zig source/tests) and the
   C emitter on the same inputs.
3. Compare output text line-for-line, except where a divergence is explicitly documented
   in parity-mapping.md / gaps.md (for example TSIR scale/eps lines, where comparison must
   parse the numeric value, not compare literal text; and emit csl_text_buffer JSON control
   bytes, where comparison must parse JSON, not compare raw bytes).
4. Compare error codes: CSL_ERR_UNSUPPORTED / CSL_ERR_INVALID / CSL_ERR_TOO_LARGE must match
   the Zig rejection for the same input, and must never be presented as a successful body.
5. On error, discard the caller buffer; partial text is not a body and must not be compared
   as one.
6. Separate steps, after text parity: cslc compile of emitted CSL (where a toolchain is
   available), then reference-interpreter / simulator execution, then, only if hardware
   is available, physical Cerebras execution. None of those steps was run in this pass.

## emit/csl

- csl_compile_sections: feed a two-section CSL text; expect exact separator strings and
  body extraction, including empty-section rejection.
- csl_spec: validateOutput on a valid layout+PE program (pass), a program missing required
  markers (fail), unbalanced braces (fail), PE before layout (fail); Q4K/Q8_0 validation
  and element counts against Zig constants.
- csl_storage_emit: given a caller global list, expect exact buffer/pointer/export text,
  bound-storage-only filter, index-with-fallback name rule.
- csl_text_buffer: write, integer write, JSON escaping; overflow case: Zig fixed-size fails
  closed without advancing, C grows to 1 GiB then CSL_ERR_TOO_LARGE. This is a documented
  divergence, test must record it, not hide it.
- emit_csl / emit_csl_core: given a CslKernelDesc for each core pattern, expect two-section
  emission, contract check, layout then PE, MAX_OUTPUT 512 KiB enforcement. Note the C
  facade also reaches dense_gemv and semantic patterns that Zig core does not dispatch;
  test must record that addition separately.
- emit_csl_attention: streaming, decode, tiled descriptors; expect Q/K/V, sliding-window,
  decode-position markers validator requires. Per-variant body detail beyond those markers
  is template-only in C and must be listed as failing byte parity until the full Zig
  template text is ported.
- emit_csl_classify: patternContractValid rules for every pattern, exact pass/fail;
  csl_classify_ir must return CSL_ERR_UNSUPPORTED for any IR input (no IR module in scope).
- emit_csl_dense_gemv: layout and PE text compared line-for-line against Zig (params,
  memcpy and collectives_2d imports, f16 activation/weight, f32 partial/output, widen loop,
  row loop, reduce_fadds, single-row completion guard).
- emit_csl_dequant / elementwise / fused / fused_ffn / gather / kv_cache / linear_attn /
  matmul / matmul_q4k / reduction / reduce_dist / rope / sample: for each, a descriptor
  for that pattern; expect the markers listed in parity-mapping.md (Q4K constants, @range
  loop, dequant marker, c2d_params, reduce_fadds, indices/table/output, cache/kv_ markers,
  Q*K*V product, accum marker, QK_K_BLOCK_BYTES/dequant_b_tile, reduce_color, fabin/fabout
  DSD markers, cos/sin markers, logit marker). Full Zig template text beyond those markers
  is not claimed; test must compare full text and record every differing line.
- emit_csl_exec_v1: op table lookup and phase permission for every op, paired-gate set
  exactly (silu_gated, sigmoid_gated, gelu_gated, o_gate), phase parsing, step-to-kernel/
  launch lowering over caller-parsed steps; JSON manifest input must return
  CSL_ERR_UNSUPPORTED, not a fabricated plan.
- emit_csl_host: WSE-3 constants, ModelConfig, SRAM estimates against Zig formulas,
  effective KV layer count with alias de-duplication, rope num_pairs formula, compilation
  manifest JSON field-for-field; Python runner scaffold must contain load_and_stage_weights,
  memcpy_h2d, init_state, STATE_BUFFERS. Full per-launch runner body is compact equivalent,
  compare and record differences.
- emit_csl_host_compile_rewrite / rmsnorm_pack: f16 rewrite (f32 to f16 on identifier
  boundaries, then exact replace-all list), fused-GEMV f32 reduction restore (exact replace
  list), f16 output-pack rewrite (CHUNK_WORDS 128, CHUNK_COUNT 21, exact insertions);
  needle absent must give InvalidIr, overflow must give OutputTooLarge.
- emit_csl_host_compile_source: fixture-presence table, including dense_gemv absent
  (NULL in Zig patternSource, sections via emitDenseGemvSections path); embedded WGSL
  fixture sources themselves must return CSL_ERR_UNSUPPORTED, not fabricated source.
- emit_csl_host_plan: CompileTarget/BindingMetadata/CslcPlan, discovery labels, makeCslcPlan,
  compile-target metadata/params JSON, host-plan artifact JSON (Zig field names), structural
  validation (schema 3, wse3, explicit_host_plan). Full JSON-schema validation is not ported.
- emit_csl_host_runtime: WeightMapping/StateBuffer/RuntimeConfig, runtime-config JSON
  (Zig field names), runner scaffold markers (decode_loop, position, sliding_window,
  EOS_TOKEN_ID, safe_run, TimeoutError, SRAM_OVERFLOW, sram_pre_check). Per-launch code
  generation and RDRR parsing not ported.
- emit_csl_ir_walk: barrier names, barrier predicates over descriptor expression kinds,
  indent/integer writers, lane-mode shim text, uniform-global-base predicate. Full walker
  functions must return CSL_ERR_UNSUPPORTED.
- emit_csl_layout: layout text for every core pattern; element-wise, reduction, dense GEMV
  compared line-for-line; other patterns compared and every differing line recorded.
- emit_csl_maps: unary/binary/assign operator text, math builtin table (13 entries),
  inline-expansion set (11 names), unsupported builtin set (textures, atomics, derivatives,
  subgroups), logical operators and/or.
- emit_csl_mem_plan: deriveGrid scan with exact betterGrid preference (fewer PEs wins;
  tie: smaller width, or smaller |width-height| when prefer_square), plan JSON field-for-field
  (schemaVersion, artifactKind, target, contract, grid, peCount, residencyMode, totals,
  utilizationPct, fits, buffers). planForGrid auto-derivation from ModelConfig + HostPlan
  is not ported; caller-supplied totals/mode must be tested as such.
- emit_csl_semantic_ops: semantic pattern set, RMS epsilon 0.000001, layout/PE text per
  pattern with semantic formulas. TSIR kernel-body templates are not in this file; bodies
  are descriptor templates, compare and record differences against TSIR bodies.
- emit_csl_simulator: simulator plan/result JSON (Zig field names/enums/constants),
  launcher script text failing closed on missing driver, structural validation. Driver
  execution must return CSL_ERR_UNSUPPORTED.
- emit_csl_toolchain: Makefile emission (cslc header, fabric dims, per-target rules,
  --memcpy --channels=1, all/clean targets, troubleshooting notes); text is structural
  equivalent, compare and record every differing line.
- emit_csl_validate: pattern validation (base spec, SDK 2.10 compatibility including
  fabout/fabin output_queue/input_queue and no-fabric_color rules, per-pattern substring
  rules copied from Zig), toolchain config from environment value (empty fails, PATH
  sentinel case-insensitive, otherwise explicit path, env var DOE_WGSL_CSLC). Running
  cslc --version must return CSL_ERR_UNSUPPORTED for non-empty path, CSL_ERR_INVALID for empty.
- Smoke driver tests/csl_emit_smoke.c: UNRUN, neither compiled nor executed by emit worker.
  It must be compiled and run, and its results recorded, before it is cited as passing.

## spatial/csl

- csl_bundle_emitter: exact //--- layout.csl --- / //--- pe_program.csl --- split
  (first markers, pe marker strictly after layout marker end, trim \n\r space, both sides
  non-empty), --wgsl/--out-dir lookup, usage text, path joining. WGSL-to-CSL translation
  must return CSL_ERR_UNSUPPORTED.
- csl_dense_gemv_host_plan: TARGET_IN_PER_PE 32, width/height/in_dim_per_pe arithmetic
  (ceil div, even ceil div, clamps), 3 binding rows, with max_rect_dim = 512 (CSL_SPATIAL_MAX_RECT_DIM,
  confirmed from csl_spec.zig:45) and with other max_rect_dim values to prove the arithmetic
  is not hardcoded to 512.
- csl_host_plan_bindings: all 9 tables plus dense table, pattern dispatch including
  reduction alias to rmsnorm and three gated aliases, f16 lane rerouting rule, unknown
  pattern returns found=0 (matching Zig null return, not an error).
- csl_host_plan_materialize: capacity constants, compile pattern mapping
  (prefill_q4k_gemv to fused_gemv_dequant), compile element mapping (dense_gemv/sample
  force f32), reserved dirs (compiled, driver-logs), kernel name test, stale-dir decision,
  default driver path rule, compile-target to targets-descriptor conversion (base kernel
  falls back to kernel name, phase NULL when absent), pe/layout/targets metadata emission
  into caller buffer. Host plan, memory plan, runtime config, simulator plan, launcher
  emission must each return CSL_ERR_UNSUPPORTED.
- csl_host_plan_tool: argument parsing/validation, usage text, phase specialized kernel
  list and prefill/decode expansion, lm_head detection, dense GEMV hidden tile expansion
  (32-wide tiles, tile name <name>_width_tile_x<start>_w<count>), compile parameters for
  every pattern branch listed in parity-mapping.md, maxModelFeatureDim, state buffer kind
  mapping. Execution JSON lowering, host plan/memory/runtime/simulator/launcher emission,
  cslc plan construction must each return CSL_ERR_UNSUPPORTED.
- csl_host_plan_tool_config: ModelConfig struct, quant format mapping (f16/q4k/q8_0),
  weight dtype mapping (f16/u8_q4k/u8_q8), activation dtype admission (f32/f16/absent)
  and scalar mapping, SHA-256 check (exactly 64 lowercase hex chars), pe range check
  (end > start), weight shape check (non-empty, every dim at least 1), typed weight mapping
  validator. Three JSON parse entry points must each return CSL_ERR_UNSUPPORTED.
  JSON defaults (ffn_expansion_factor 4, ffn_matrix_count 3, partial_rotary_factor 1.0)
  are documented but are the unported parser's job, not this test's.
- csl_pe_program_metadata: all four declaration shapes (typed var, @zeros var with
  duplicate-name suppression, pointer var [*]elem = &backing, @export_symbol), param/const
  lines with defaults (param without default not recorded), layout @export_name lines
  with parenthesis-depth type scanning and mutable flag, device_function/device_variable
  classification, export resolution (pointer to backing to declaration), three JSON emitters
  with byte-for-byte layout matching Zig writers (field order, commas, trailing newlines,
  phase: null handling). Parsed strings borrow source buffer; test must prove no use-after-free
  on source buffer release.
- csl_sim_runner: usage text, argument parsing (--plan, --driver-executable, --result-json,
  --help), driver resolution order (explicit flag, then DOE_CSL_SIM_EXECUTABLE, then fail
  closed), artifact path resolution against plan directory, default result path
  (<trace>.result.json), termination-to-result status mapping (exited 0 succeeds, anything
  else fails, only exited carries exit code), trace existence check backing trace_produced.
  Simulator plan parsing/validation and result artifact emission must return
  CSL_ERR_UNSUPPORTED; driver execution must return CSL_ERR_UNSUPPORTED and must never
  spawn a subprocess from library code.
- Test driver tests/csl_spatial_tests.c (280 lines, UNRUN-marked, with canonical-512 checks
  and trace-existence checks): compile and run, record counts, before citing as passing.

## TSIR CSL-only

- emit_csl: skeleton and semantic layout/pe_program, compared line-for-line except
  documented divergences; digest must return CSL_ERR_UNSUPPORTED; descriptor-hash and
  function-index range checks are caller-owned and must be tested as caller-owned, not
  as emitter behavior; semantic metadata blocks supplied pre-rendered, NULL meaning only
  empty count block.
- emit_kernel_body_attention: full_per_pe and kv_axis_sharded, single-Q and multi-Q,
  tile_param, layout_coordinates, compared line-for-line except parsed scale/eps lines.
- emit_kernel_body_conv1d: with/without bias, causal left-only pad, compared line-for-line
  except parsed scale/eps lines.
- emit_kernel_body_gated: gelu, silu, sigmoid, per-kind helper names, compared line-for-line
  except parsed scale/eps lines. Silu/sigmoid shared clamp block must produce the same
  text as Zig for both kinds.
- emit_kernel_body_kv: slot_sharded write and read, export lists exactly (write:
  key_proj/val_proj/key_cache/val_cache/position; read: key_cache/val_cache/key_output/
  val_output), compared line-for-line except parsed scale/eps lines. full_per_pe must
  return CSL_ERR_UNSUPPORTED.
- emit_kernel_body_l2_normalize: two-pass, f32 and f16 sqrt_nr forms, compared line-for-line
  except parsed scale/eps lines.
- emit_kernel_body_linear_attention: shared-norm only, compared line-for-line except
  parsed scale/eps lines; per_head and dt_bias must return CSL_ERR_INVALID; no multi-Q
  entry point, matching Zig.
- Shared compute tail: every body plus both attention paths and both KV bodies must
  emit exports in the caller's original order, with @export_symbol(compute) last in the
  tail block. This is the regression test for the KV write/read swap incident recorded
  in tsir-mapping.md.
- Float formatting divergence, all TSIR bodies: Zig {e} shortest scientific vs C %e
  6-digit form (0.125 becomes 1.250000e-01). Test must parse scale/eps lines numerically,
  not compare literal text, and must record the divergence.
- Test driver tests/tsir_csl_tests.c (283 lines, UNRUN): compile and run, record counts,
  before citing as passing.
- Separate steps after text parity: CSL-compile, then reference-interpreter, then
  physical execution if available. None was run in this pass.

Per-worker test definition files, retained as detail: emit-tests.md, spatial-tests.md, tsir-tests.md.
