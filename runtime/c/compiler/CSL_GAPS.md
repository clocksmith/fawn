# CSL C port, gaps (no hidden gaps)

Label: CSL C port, untested / not run, parity not verified.
Nothing was pushed. This file lists every divergence, unsupported path, and untested
area known from the three worker mappings. If a gap is not listed here, it is unknown,
not closed. Do not treat absence from this file as proof of parity.

## Gaps that apply to the whole CSL port

- G-NO-ZIG-CROSSCHECK: No Zig toolchain exists on this VM, so no C output string in
  this port was compared against a Zig binary's output. All "exact" claims rest on reading
  Zig source at d25e075. Nothing here was run on GPU or physical Cerebras hardware, and
  no cslc compile was performed. Emitted CSL is host/simulator-parity text as written
  on Linux only, and even that text parity is untested.
- G-IR: There is no C WGSL IR module in scope. Kernel classification, layout, and PE
  emission take descriptors (CslKernelDesc, TsirCslKernelDesc), not a Zig ir.Module.
  Anything that derives pattern, buffer indices, tile parameters, or body detail from
  a real IR module is not ported and returns CSL_ERR_UNSUPPORTED where it would otherwise
  be needed. A future C IR port can fill descriptors from real IR without changing emitters,
  but until then descriptor-filled inputs are caller assertions, not Zig-derived facts.
- G-JSON: No general JSON parser is ported. Host-plan, runtime-config, memory-plan,
  simulator-plan, bundle, execution, and manifest JSON parsing return CSL_ERR_UNSUPPORTED
  where listed below. JSON emission (field names and order) is ported in several places,
  but emission is not parsing, and round-tripping through a real parser is untested.
- G-DRIVER: Actually running cslc, a simulator driver, or a physical backend is not
  performed by this library. Driver/cslc execution returns CSL_ERR_UNSUPPORTED where
  listed below. Driver resolution order and launcher script text are ported, execution is not.
- G-WIRING: This port is not wired into runtime/c (no sources.txt entry, no compiler
  Makefile entry, no repo test naming, no aggregate make check over an integrated tree).
  It is a local patch set in ~/workspace/doe-c-work/csl-port/, ready for one future
  snapshot-push, not an integrated build. Strict-flag wiring (for example -Wconversion,
  -Wswitch-enum across pushed sources) was not done for this CSL set, except for the
  worker-reported syntax/compile checks in test-results.md.
- G-PARTIAL-MEANS-NOT-PARITY: emit/csl done 7 / partial 30, spatial/csl done 3 / partial 5,
  TSIR CSL done 6 / partial 1, all as files. Every partial row in parity-mapping.md lists
  what is not ported. "Missing 0 as files" does not mean parity.

## emit/csl gaps

- IR walk: full IR statement/expression walker (stmt, expr, functionBody, writeType,
  helperFunctions, comptimeExports, etc.) returns CSL_ERR_UNSUPPORTED. Only barrier names,
  barrier predicates over descriptor kinds, indent/integer writers, lane-mode shim text,
  and uniform-global-base predicate are ported.
- IR classification: csl_classify_ir returns CSL_ERR_UNSUPPORTED. Callers supply pattern
  and info; patternContractValid rules themselves are copied exactly.
- Embedded WGSL fixtures: fixture sources themselves and WGSL-to-IR analysis are not
  ported. Only the fixture-presence table is ported, with dense_gemv correctly absent
  (Zig patternSource returns NULL for dense_gemv; dense sections come from
  emitDenseGemvSections path).
- Python runner: full per-launch code generation in emit_csl_host and emit_csl_host_runtime
  is a compact equivalent/scaffold, not the Zig text. Markers checked by Zig tests are
  present; body text beyond those markers is not claimed byte-identical.
- JSON manifest / execution lowering: lowerJsonToHostPlan, lowerManifestExecutionToHostPlan,
  LayerPattern / numKvSharedLayers / sliding-window metadata lowering from a manifest,
  and WGSL-filename-to-op mapping as a separate table are not ported. Only op-name mapping
  via the single op table is ported.
- Kernel bodies: attention, fused, fused_ffn, matmul (SUMMA broadcast loop), layout for
  matmul/attention/kv patterns, reduction lane-preserving pre-barrier loop lowering,
  RoPE interleaved vs rotate-half selection, and several other kernel bodies are pattern
  templates or skeletons, not full Zig template text. See parity-mapping.md per-file rows.
- Mem plan: planForGrid auto-derivation from ModelConfig + HostPlan and auto-choice of
  full_resident vs layer_streaming are not ported; caller supplies buffer totals and mode,
  stream stages caller-supplied (empty by default).
- Semantic ops: TSIR kernel-body templates imported by emit_csl_semantic_ops are not
  ported in emit/csl; PE bodies are descriptor templates, not TSIR bodies. TSIR CSL bodies
  themselves are ported separately by the TSIR worker; integration between the two is untested.
- Simulator / toolchain / validate: driver execution, cslc --version execution, plan-to-typed-
  contract parsing beyond structural checks, and full JSON-schema validation are not ported.
  Toolchain Makefile text is a structural equivalent, not byte-identical to Zig.
- Text buffer divergence: Zig fixed-size fails closed without advancing on overflow;
  CslBuf grows to 1 GiB before CSL_ERR_TOO_LARGE. JSON escaping escapes every control byte
  as \u00XX; Zig writes non-\n\r\t controls raw. Valid JSON either way, bytes differ at edges.
- Core dispatch addition: dense_gemv and semantic patterns are reachable through the C
  core facade although Zig core does not dispatch them. This is an addition, not a Zig
  behavior, and must be tested as such before any claim that core matches Zig.
- Marker buffer: csl_compile_sections marker buffer is 256 bytes, not Zig 128.
- Direction/arch name helpers in csl_spec are additions, not Zig functions.

## spatial/csl gaps

- WGSL-to-CSL translation: csl_spatial_bundle_translate_wgsl returns CSL_ERR_UNSUPPORTED.
  Bundle splitting, option lookup, usage text, and path joining are ported; translation is not.
- File/directory creation: file writing and directory creation are caller job in spatial
  modules. Functions emit text into a caller CslBuf or return CSL_ERR_UNSUPPORTED; they do
  not write files. writeFile/writeExecutableFile collapsed into one caller-responsibility
  rule; the only Zig difference was chmod, which belongs to the layer that actually creates files.
- Host plan, memory plan, runtime config, simulator plan, launcher emission (materialize):
  each returns CSL_ERR_UNSUPPORTED. Only pattern/elem mappings, metadata emission, and
  target-descriptor conversion are ported.
- Execution JSON lowering (tool, manifest and steps modes), host plan/memory/runtime/
  simulator/launcher emission, and cslc plan construction: each returns CSL_ERR_UNSUPPORTED.
- JSON parse entry points (tool_config): bundle model config, bundle activation dtype,
  bundle weight mappings: each returns CSL_ERR_UNSUPPORTED. JSON defaults
  (ffn_expansion_factor 4, ffn_matrix_count 3, partial_rotary_factor 1.0) are documented
  in the header but are the unported parser's job.
- Simulator plan parsing/validation and result artifact emission: each returns
  CSL_ERR_UNSUPPORTED. Driver execution deliberately returns CSL_ERR_UNSUPPORTED and never
  spawns a subprocess from library code.
- Compile source generation (materialize): emitPatternSectionsForElem is out of scope, so
  materializeCompileSources has no C equivalent beyond pattern/elem mappings and metadata emission.
- Deliberate behavior differences from Zig, documented in spatial-mapping.md: error-enum
  mapping, sim runner rejecting unknown args, zero-grid-dimension handling, the 64-entry
  silent-drop cap kept for byte fidelity, and fixed-size target buffers.
- BindingShape.kind omitted: uniformly "csl_array" default in these tables, verified against
  emit_csl_host_plan.zig:53-74, so omitting it loses nothing in these tables, but a future
  table with a non-default kind would need it restored.

## TSIR CSL gaps

- Digest: emit_csl digest always returns CSL_ERR_UNSUPPORTED (hashing C sources would
  give a different digest from Zig).
- Descriptor-hash comparison and function-index range checks: caller-owned, no stored
  hash or function list in the descriptor seam.
- Semantic metadata blocks: must be supplied pre-rendered, NULL meaning only the empty
  count block.
- KV full_per_pe: intentionally missing, CSL_ERR_UNSUPPORTED, because that path lives in
  excluded emit_kernel_body.zig (non-CSL, already done separately, not in this scope).
- Linear attention: per_head and dt_bias return CSL_ERR_INVALID; no multi-Q entry point,
  matching Zig, but that matching is by design, not by a run test.
- Float literals: Zig {e} shortest scientific form vs C %e 6-digit form
  (0.125 becomes 1.250000e-01). Parsed value same, literal text NOT byte-identical.
  Parity tests must parse scale/eps lines.
- Inherited Zig quirks preserved, not fixed: KV i16 pe_id vs skeleton u32, sharded 8-space
  mask indent, hardcoded f32 layout export, binding names copied verbatim without
  sanitization, u32 dims printed into i16 CSL consts without extra range gates.
- On error, caller-owned buffer may hold partial text and must be discarded; no partial
  text is presented as a body.
- KV write/read swap incident: first refactor swapped write/read export lists; syntax
  check caught it, fixed by function, export order verified by grep, syntax check re-run
  clean. Behavioral parity tests must still be run before trusting refactored output.

## Simplification risks (documented, not hidden)

- Emit simplifications that rest on verbatim-copy arguments (shared PUT/JSON/PE-preamble/
  scalar helpers, shared layout skeleton, shared attention core, shared contract/validate/
  exec tables, wrapper delegation) have not been proven by a run diff. A run diff of
  pre-refactor vs post-refactor output is still required before those identity claims
  can be upgraded from "by construction" to "verified".
- Spatial simplifications that rest on table/branch equivalence arguments (S1 bindings
  table, S2 merged parameter branches, S3 shared target shell, S4 shared helpers) have
  not been proven by a run diff either. Names, order, and values are claimed unchanged;
  a run test is still required.
- TSIR simplifications that rest on shared-tail/import/header arguments have the same
  status: export order verified by grep after refactor, but no behavioral run.
- Cross-emitter sharing across host plan, layout, mem plan, simulator, and kernel-body
  emitters was NOT attempted by the spatial worker (other workers' scope), and was only
  partially attempted by emit/TSIR workers within their own scopes. A future consolidation
  pass could still find more shared helpers, but must not be assumed to exist already.

## Standing non-CSL gaps, NOT closed by this port

These were the remaining Doe items before this CSL order, and this CSL port does not
claim to close any of them:
- DXIL bounded frontend/HLSL support.
- Windows DXC runner explicitly unimplemented.
- Production wiring: TSIR/DXIL (and now CSL) outside the production compiler library
  (sources.txt, libdoe_compiler.so, strict-flag wiring).
- Documented semantic differences: frontend depth limits, softmax failure behavior,
  embedded-NUL names, shader admission differences.
- Full cross-platform runtime parity not established; no physical backend qualification claimed.
- Native API: 16 names that were stubs, delegation-only paths, or no-ops out of 266
  (status at d25e075 not re-verified in this CSL pass).
- Registry gaps R0/R0b, emitterCodeDigest E22, config knobs E23, descriptorHash outside
  TSIR, zeroed and caller-supplied (status at d25e075 not re-verified in this CSL pass,
  except where TSIR CSL digest is explicitly CSL_ERR_UNSUPPORTED above).
- Fawn launcher gap, as its own packaging fix, if still present.

## Untested areas, all of them

- Behavioral parity tests: not run for any of the 52 files.
- CSL compile (cslc): not run.
- Simulator / reference interpreter: not run.
- GPU / physical Cerebras execution: not run.
- macOS, Windows, aggregate suites, production wiring: not run.
- Pre-refactor vs post-refactor output diffs for simplifications: not run.
- Integration between emit/csl semantic ops and TSIR CSL kernel bodies: not run.
- Integration between spatial host-plan tool/materialize and emit/csl host-plan/runtime: not run.
