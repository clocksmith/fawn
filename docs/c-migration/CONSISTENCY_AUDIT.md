# Non-CSL C consistency audit ledger

Reconciled through `d6e8fa805` (`2026-11-10`; initial ledger `2026-10-26`). This
is an explicit coverage ledger, not a replacement for component contracts, a
whole-codebase review certificate, or a parity completion claim. CSL remains
quarantined. Retained sources and failing independent oracles remain unchanged.
An export count or a successful build does not verify a whole layer.

## Inspected paths and concrete findings

Paths below are relative to `runtime/c/` unless explicitly qualified.

| Owner / inspected paths | Concrete finding | Implementation and evidence | Publication status |
|---|---|---|---|
| Resource recording: `resources/src/commands.c`; native queue/Metal/D3D copy adapters | Empty texture copies dropped resource leases and destroyed-at-submit validation | Texture-to-texture differential fixed in `97becb4d`; empty buffer/texture correction in `d1d1a0ae`. Owner lifetime controls and CPU Vulkan public controls pass. The latter also fixes retained Zig behavior: original C/Zig reject 0/12 destroyed cases, Dawn/patched C reject 12/12. | Published on `main` (`40b04120`, `d1d1a0ae`) |
| Recording state: `resources/src/{commands,bundles}.c`, `native/src/{errors,debug_markers,render_bundles}.c` | Finished encoders replayed a stale OOM instead of finished-state Validation | `5ba4ba88`: first Finish retains OOM; subsequent valid/malformed marker, bundle unbind and repeated Finish report Validation. Real allocation-failure nested error-scope controls and owner suite pass. | Published on `main` (`5ba4ba88`) |
| Bundle storage: `resources/src/bundles.c`, retained `runtime/zig/src/runtime/render/render_bundle.zig` | C-only 4096-operation ceiling rejected valid retained behavior | `491bd09d`: dynamic draw growth with overflow checks; 8193 allocation-free setters, 4097 draw snapshots and growth-OOM cleanup controls pass. Existing pass draw budgets remain enforced. | Published on `main` (`491bd09d`) |
| Mapping phase: `resources/src/{bindings,render_state,render_usage,commands,render}.c` | Mapping availability checked during recording instead of submission | `491bd09d`: existing submit-phase owners retain mapped/pending rejection. 32 ownership controls cover uniform/storage, vertex/index/indirect and bundle replay. Six public CPU Vulkan cases include exact compute readback after unmapping between Finish and Submit. Pending primary-buffer states are defensive internal tests, not legal public map-usage claims. | Published on `main` (`491bd09d`) |
| Compiler syntax / semantics / targets: `compiler/src/{internal.h,parse.c,validate.c,uniformity.c,spirv.c}`, `compiler/targets/{metal,hlsl}/control.c`; retained parser/IR/target switch owners | WGSL `switch` entirely absent in C | Confirmed old C parser rejection; same minimal retained Zig pipeline admitted on CPU Vulkan. Parser, typed semantics, uniformity and three targets implemented. SPIR-V validation and textual host controls pass; C and retained Zig execute the same exact 16-word CPU Vulkan oracle. | Published on `main` (`b42137eb9`) |
| Compiler adapters: `compiler/src/{cli.c,cli_text.c,cli_specialization.c}` | Fixed adapter pools reject larger valid shaders despite caller-sized typed ABI | Canonical checked exhausted-pool growth shared by CLI and opt-in native shader adapters. CLI growth/fault controls and shader snapshot/OOM controls pass; C and Zig execute 2200 updates identically. Explicit source bounds remain. | Published on `main` (`b42137eb9`) |
| Compiler name spans: `compiler/src/{sema_call.c,spirv_module.c,compile_spans.c,legacy_reflection.c}` | 63-byte legacy name bounds leak into constructors, SPIR-V names and entry selection | Source-backed names retained through parser, typed reflection, selection and artifact lifetimes. Private target symbols map collision-free to bounded metadata; legacy projection explicitly rejects overflow. Three-target compiler and shader ownership/sanitizer controls pass; C and Zig distinguish two 97-byte entries. | Published on `main` (`b42137eb9`) |
| Compiler diagnostics: parser directive/attribute admission, uniformity owner, and failed-module notice retention | Valid `diagnostic` controls and lexical `@diagnostic` attributes rejected; warning/info notices discarded on subsequent fatal WGSL failure | Global `diagnostic(severity, rule);` controls and lexical `@diagnostic(severity, rule)` attributes on functions, compound blocks, and `if`/`switch`/`for`/`while`/`loop` statements implemented through canonical uniformity without workspace ABI growth; additive ABI18 publishes immutable warning/info notices; pre-fatal notices emitted before the first fatal WGSL source failure (`DOE_COMPILER_PARSE_ERROR`, `DOE_COMPILER_SEMANTIC_ERROR`, `DOE_COMPILER_UNSUPPORTED`) are retained after clean FP restoration through `doe_shader_create_module_diagnostics` and `wgpuShaderModuleGetCompilationInfo` alongside the terminal `Error` record (`count + 1`). Compiler, shader fault-sweep, native Metal, JS bridge, and UBSan controls pass. Multi-error recovery after the first fatal failure is not claimed. | Published on `main` (`9b219152d`, `d6e8fa805`, current) |
| Native render interfaces: `native/src/render_interfaces.c`, Metal pipeline admission | NULL vertex spans traversed before validation | Shared preflight implemented; identical-object old/new host proof changes SIGSEGV to Validation, including async controls and valid aggregate boundaries. Exact staged-tree full Metal bridge passes with incomplete transfer work excluded. | Published on `main` (`b42137eb9`) |
| Queue/Metal transfers: `native/src/{metal_texture_write,texture_copies,d3d12_texture_write}.c`, `backends/metal/src/texture_transfer.c` | Empty writes skip CopyDst validation; linear/sRGB copies reject compatible formats | Canonical admission and typed compatible-copy repairs authored. Private old/new copy proof passes; allocation-free canonical queue admission on all three native backends and corrected after-creation test baseline pass resource host checks and integrated Metal bridge; typed Vulkan/D3D caller controls prove no staging before validation and balanced reentrant lifetimes. Private/public old/new proofs preserve the original failures. | Published on `main` (`9b219152d`) |
| Vulkan constructor diagnostics: `native/src/{render_pipeline,queries}.c` | Native adapter drops backend first-operation/code detail | Canonical failure transport implemented. Injected graphics/query failures retain exact operation and signed code after cleanup and deferred graphics callback. Existing status classification preserved. | Published on `main` (`9b219152d`) |
| Runtime CLI kernel cache and receipts: `runtime-cli/src/{kernel,main}.c` | Path-keyed cached execution may disagree with reread source receipt after file replacement | Old CPU reproduction executes A while reporting B after path replacement. Immutable exact-byte cache identity and retained source receipts now pass isolated A/B/C mutation, per-execution hash and allocation rollback controls. Prior benchmark evidence is not invalidated without actual mutation. | Published on `main` (`78b710d71`) |
| CLI asynchronous acquisition/scopes: `runtime-cli/src/execute.c` | Stack callback userdata survives an unsuccessful wait without settlement | Repaired in `83dcd088e` via heap-leased `runtime_reply` callback state, synchronized error transport, terminal device teardown and host lifetime controls; extended in `917cf4c83` and `a45ee77c1`. | Published on `main` (`83dcd088e`) |
| Numeric evaluation/policy: `runtime-cli/src/{numeric_eval,numeric_policy,numeric_stability}.c` | Candidate bias narrowed before double accumulation; malformed required policy flags silently weaken admission | Exact host before/after confirms stable-token route correction; strict required-policy typing controls pass. Canonical schema binds nonempty/unique text arrays; repeated proof objects remain accepted. | Published on `main` (`9b219152d`) |
| Default routes and Zig packaging references: `packages/doe-gpu/scripts/*`, `runtime/bridge/onnxruntime-ep/Makefile`, `runtime/c/tools/build-native-package.py` | Five charged production scripts retained Zig build/staging paths | Default C routing reconciled in `838b6e7e1`; remaining five Zig packaging/build references retired in `22c6d4113` (`zig_references_in_charged_production == []`), with test-span audit materialized in [`test-span-audit.json`](test-span-audit.json). | Published on `main` (`838b6e7e1`, `22c6d4113`) |
| Bridge production deduplication: `runtime/bridge/onnxruntime-ep/src/doe_ort_ep.cc`, `runtime/bridge/webgpu-addon/doe_napi_*.c` | Repetitive per-operator counter/attribute/consumer/epilogue boilerplate and duplicated N-API limits/adapter-info/callback-binding, encoder/pass immediates/debug/indirect, buffer-data, compilation-hint, bind-group-layout, device-descriptor/release, and native-direct method-registration paths inflated charged bridge files | Unified 15-operator metadata + indexed atomic counters, attribute/consumer/epilogue helpers (`2,856 -> 2,174` lines, `-682`), shared `doe_napi_queue.c` helpers (`1,923 -> 1,592` lines, `-331`), and deduplicated nine additional N-API addon translation units (`doe_napi_{caps,render,nd_immediates,nd_encoder,nd_creators,instance,shader,pipeline,nd_device}.c`, `-505` addon lines) without modifying `doe_napi_internal.h`. Mock-ORT UBSan checks, `doe-gpu` N-API addon build (60 struct layouts, 18 enum tables), contracts, smoke, and live Metal C-provider integration checks pass; net charged production drops from `172,731` -> `171,881` -> `171,679` lines (`-1,052` cumulative net after compiler/shader/native diagnostics and opt-in Vulkan candidate additions). | Published on `main` (`d6e8fa805`, current) |

## Explicit remaining coverage boundaries

- Accountable line-count reduction ([`QUALITY_TARGET.md`](QUALITY_TARGET.md),
  [`test-span-audit.json`](test-span-audit.json),
  [`c_quality_target_audit.py`](../../bench/gates/c_quality_target_audit.py)) is
  **not established**: charged production stands at `171,679` lines against the
  `77,949` headline ceiling (`93,730` lines above ceiling), normalized C
  implementation stands at `171,655` lines against the `71,703` ceiling (`99,952`
  lines above ceiling), and `human_readability_and_span_signoff` remains `false`.

- A backend audit inspected native backend/status/failure/stage/pipeline/
  binding/queue/query paths, Metal transfer and graphics paths, D3D12 recording/
  texture/graphics paths, and Vulkan render/transfer/completion portions. It did
  not inspect every portion of those files, all loaders, SDK surfaces, caches,
  timestamps, indirect guards or complete feature tables. No whole-layer claim.
- Full compiler language parity is **not established**. Global `diagnostic`
  directives, lexical `@diagnostic(severity, rule)` attributes on functions,
  compound blocks, and control statements, and pre-fatal warning/info notice
  retention before the first fatal WGSL source failure pass compiler, shader,
  native, and UBSan checks without workspace growth; multi-error recovery after
  a fatal failure and uninspected WGSL language corners remain explicit
  boundaries; neither file counts nor existing source inventories close that
  denominator. Additional inspected findings must be logged.
- The original MSAA scissor correctness oracle remains failed. Raw Vulkan, C and
  retained Zig produce the same 64-pixel result on the pinned CPU driver; official
  Mesa scissor-plane fix attribution is documented in
  [the diagnostic note](../../runtime/c/native/MSAA_SCISSOR_DIAGNOSTIC.md).
  A fixed-driver rerun and physical confirmation are pending; no tolerance or
  runtime workaround has been substituted.
- Old compiler capacity/expectation gates are not blanket-waived. Changes to an
  intentional old rejection require a named new contract and focused controls;
  frozen historical artifacts are not rewritten.
- Physical Metal/D3D12/Vulkan and full Fawn browser qualification remain separate,
  machine-specific work. No new physical run is authorized by this ledger.
- Publication hygiene retains its historical 3464 findings across 15 unchanged
  evidence files. Scoped changed-file privacy checks pass; historical findings
  have not been deleted, exempted or called a passing full hygiene gate.

## Integrated checkpoint evidence

The current switch/name/capacity tree passes the mandatory 13-owner build, CLI/
module build, mutable-globals check (386 artifacts in the next isolated staged tree), and software-Vulkan resource
lifetime consumers. Focused switch SPIR-V validation, emitted MSL/HLSL host
execution, long-name shader ownership/ASan/UBSan and actual CPU Vulkan consumers
pass. LeakSanitizer is unavailable under the host ptrace environment. These checks
do not qualify physical GPUs or the pending diagnostic and backend repairs.

## Current reproducible checks

With the documented CPU Vulkan ICD and compiler dependencies available:

```sh
make -C runtime/c/resources check
make -C runtime/c check
```

The native Makefile's normal `check` includes `encoder-finish-oom` and
`mapping-timing`, alongside other tests. Use the chosen CPU ICD and
`LP_NUM_THREADS=1`;
that full suite still contains the preserved independent MSAA failure.
