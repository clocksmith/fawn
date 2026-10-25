# doe-c-runtime: C command-stream runtime CLI

`doe-c-runtime` executes a runtime command stream (`examples/*_commands.json`)
through the public WebGPU C ABI of the C native library (`libwebgpu_doe_c`,
built by [the native owner](../native/README.md)) and writes the same trace
JSONL rows and trace meta that `doe-zig-runtime` writes. It is the first C
replacement for a [stage 6](../../../docs/c-migration/MIGRATION.md) consumer:
the `doe-zig-runtime` CLI that `createDoeRuntime().runBench` in
`packages/doe-gpu` and the bench modules call. The package's
`createDoeRuntime()` runs it when the selected provider is the C library
(`libraryFlavor` `doe-c`) or when `{ runtime: "c" }` is passed, from `DOE_BIN`
or `runtime/c/runtime-cli/bin/doe-c-runtime` (`make bench-bin`); it never falls
back to the Zig CLI, and run results carry `runtimeImplementation`.

```bash
make -C runtime/c/runtime-cli            # builds $(BUILD)/doe-c-runtime and the native library
make -C runtime/c/runtime-cli check-host # host-only tests
make -C runtime/c/runtime-cli check-surface # real display required
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json make -C runtime/c/runtime-cli check
$(BUILD)/doe-c-runtime --commands examples/rmsnorm_tree_commands.json --execute \
  --backend native --kernel-root bench/kernels --trace-jsonl out.jsonl --trace-meta out.meta.json
```

On an actual x64 Windows SDK developer prompt, the owner builder consumes the
native owner's audited DLL output and produces `doe-c-runtime.exe` and
`doe-c-module-runner.exe` with their DLL dependencies:

```text
python runtime/c/runtime-cli/tools/build_windows.py --out <fresh-output> --native-dir <native-build-output>
```

The canonical source inventories are shared with Make. Windows uses D3D12 adapter
selection and trace identity, wide arguments and UTF-8 file paths, a real HWND
surface host, and shared byte-preserving output ownership for CLI and modules.
The builder runs host parser/hash/numeric controls, source-bound module fixtures,
and normalization/Unicode/replay controls. Its receipt binds source, tools and
producer DLL bytes; those checks do not execute a device or window. Actual SDK
compilation and D3D12 execution require their own successful run, separately
from Linux memory/file-output controls. Set `DOE_BIN` and `DOE_MODULE_RUNNER` to
the produced executables; automatic package discovery uses `.exe` on Windows.

With no trace paths, `--trace` or `--replay`, stdout shows the quirk decision
and normalized command details. Execution timing is shown only for real execution.

Without `--execute`, the same parser and quirk engine produce host-only traces:
no instance, adapter or device is created, row execution fields are absent and
meta execution counters are zero. `--backend trace` selects this mode explicitly;
`--execute --backend trace` is rejected. Omitting `--commands` or `--quirks`
selects the retained embedded samples. `--quirk-mode off` disables quirks.
`--replay <jsonl>` validates every row's module, opcode, sequence, command,
kernel and hash chain, including repeats, before device creation. Missing,
extra or changed rows fail. Semantic host captures are explicitly unsupported.
`check-host-modes` interposes the actual instance ABI to prove those paths do
not open a device, with an executing control that proves the interposer works.

`--emit-normalized` writes retained command records to stdout before execution or
quirk application, including aliases, render modes and dynamic offsets. It
leaves trace paths untouched even with `--execute`; output oracles and readbacks
are incompatible. The writer shares dispatch formatting with semantic repro
bundles. Forty source-bound retained records exercise every admitted command
family, source f32 spelling and repeated sequences under the no-device guard.
Buffer-write data uses valid JSON arrays; the retained Zig printer used invalid
slice debug braces. Quoted kernel paths are JSON escaped.

## Admitted input

- Commands: `upload` (`buffer_upload`), `buffer_write` (`write_buffer`,
  `queue_write_buffer`), `barrier`, and `kernel_dispatch` with WGSL kernels,
  buffer bindings (`uniform`, `readonly`/`read_only_storage`, `storage`), 2D
  `texture` bindings (`texture_sample_type` float, unfilterable-float, depth,
  sint or uint) and 2D `storage_texture` bindings (`storage_access`
  write_only, read_only or read_write, with a `texture_format`), whose texture
  is the handle's when its usage covers the binding and its format matches, else
  one of the same shape (1x1 when missing) with the union of usages replaces it,
  as doe-zig-runtime's `getOrCreateTextureFromBinding` does, and
  `repeatSynchronization` `dependent` or `independent` (the repeats do not read
  each other's results; doe-zig-runtime then drops its Vulkan barriers, while
  webgpu.h has no barrier control, so this CLI records the same ordered
  dispatches and the provider orders them as it orders any dispatches in a
  pass). Each kind has a field allowlist.
- `dispatch` (`dispatch_workgroups`, `dispatch_invocations`) and
  `dispatch_indirect` with `x`/`y`/`z` or `workgroupCount`, as doe-zig-runtime's
  native lane runs them: one dispatch of the grid on the last kernel_dispatch's
  pipeline and bindings, else on `dispatch_noop.wgsl` from the kernel root
  without bindings; `dispatch_indirect` writes the grid to an Indirect argument
  buffer first. The device requests the adapter's `maxStorageBufferBindingSize`
  and `maxBufferSize`, as the native lane binds against device capabilities;
  every other limit is the WebGPU default.
- Copies in every direction under every doe-zig-runtime alias
  (`copy_buffer_to_buffer`, `copy_buffer_to_texture`, `copy_texture_to_buffer`,
  `copy_texture_to_texture`, `buffer_copy`, `texture_copy`, ... or an explicit
  `direction`), `texture_write` (`write_texture`, `queue_write_texture`),
  `texture_query` (`query_texture`) and `texture_destroy` (`destroy_texture`),
  for 2D single-sample textures in `r8unorm`, `rg8unorm`,
  `rgba8unorm`, `rgba8unorm-srgb`, `bgra8unorm`, `r32uint`, `r32float`,
  `rgba16float` and `rgba32float`. A copy side may name a `mip_level`: the
  texture is created with at least that many levels plus one (a texture with
  fewer is replaced) and the copy uses that level's extent. doe-zig-runtime's
  WebGPU path creates single-level textures and copies the full extent, so it
  cannot copy a level above 0; its native lane reports these copies
  unsupported. Resources follow doe-zig-runtime's WebGPU
  command path: a missing copy source is created zero-filled, a buffer too small
  or a texture of another shape is replaced (its contents are lost), an undefined
  copy format is `r8unorm`, and a buffer side's missing row pitch falls back to
  the texture side's. Textures also carry CopySrc|CopyDst so writes and copies
  keep their contents. WebGPU validates layouts: a row pitch that is not a
  multiple of 256 fails the copy with exit 1.
- `render_draw` (`draw`, `draw_call`, `draw_indexed`) and `render_pass`, as
  doe-zig-runtime's native Vulkan lane records them: `drawCount` draws of the
  default proxy pipeline (`draw_call_proxy_vertex.wgsl` and
  `draw_call_proxy_fragment.wgsl` from the kernel root, no bind groups) inline in
  one render pass that clears the color target to (0, 0, 0, 1), with the
  command's viewport, scissor, blend constant, stencil reference and index data.
  The target is a scratch texture unless `targetHandle` names an existing
  texture with RenderAttachment usage. `pipelineMode`, `bindGroupMode`,
  `encodeMode` and `bindGroupDynamicOffsets` are validated but, as in that lane,
  do not change the recorded work. `draw_indirect` and `draw_indexed_indirect`
  take the same fields; their argument record is written `drawCount` times into
  one Indirect buffer (as doe-zig-runtime's WebGPU multi-draw path does) and
  drawn with one indirect draw per record, while doe-zig-runtime's native lane
  draws them directly. Their `firstInstance` must be 0, since the
  indirect-first-instance feature is not requested. Explicit attachments,
  depth/stencil attachments, vertex buffers, bind groups and indirect-buffer
  handles are rejected.
  `sampler_create` (`create_sampler`) and `sampler_destroy` (`destroy_sampler`)
  create and release samplers keyed by handle.
- `map_async` (`buffer_map_async`) with `bytes` and `mapMode`/`map_mode`
  (`read` or `write`, default `write`), or the nested
  `{"map_async":{"bytes":1024,"mode":"read"}}` shape. The runtime reuses
  one buffer while its size and mode suffice, replacing it otherwise. Each
  command maps the requested range, waits, zero-fills write mappings and unmaps;
  read mappings leave the bytes untouched, matching the retained WebGPU path.
  Sizes must be nonzero multiples of four. The native Zig Vulkan lane reports
  mapping unsupported; C executes it through the selected WebGPU provider.
- Kernels resolve as given, then under `--kernel-root`, then with `.wgsl`.
- Semantic identity on `kernel_dispatch` (`src/operators.c`), as
  doe-zig-runtime's `contracts/semantic.zig` reads it: `semanticOpId`,
  `semanticStage`, `semanticPhase`, `semanticTokenIndex`, `semanticLayerIndex`
  and `semanticExecutionPlanHash` (or their snake_case spellings, one of each
  pair) go into the row and its hash in Zig's order and encoding. With a trace
  meta or JSONL, each such command gets an operator record in
  `<anchor>.operators.json` (anchor: the meta path, else the JSONL path) and a
  structural repro bundle `<anchor>.opNNNN.repro.{commands,meta}.json`; a
  capture (`captureBufferHandle`, `captureSize`, optional `captureOffset`)
  writes those buffer bytes after the command to `<anchor>.opNNNN.capture.bin`,
  and an out-of-range or unknown capture is recorded as `InvalidArgument`
  without failing the run. The record's `execution` object is this CLI's (no
  lane, GPU timestamp or queue family fields) and `shaderArtifacts` is empty.
  doe-zig-runtime drops a partial capture or one without semantic identity; this
  CLI rejects both.
- Numeric stability (`src/numeric_stability.c`, `src/numeric_policy.c`), as
  doe-zig-runtime's `runtime/numeric_stability` Recorder evaluates ordinary
  execution whenever it writes a trace meta: a dispatch whose op id, stage,
  phase and kernel basename match an enabled auto-detect profile of
  `config/numeric-stability-policy.json` (or `--numeric-stability-policy`) is
  evaluated after it runs. For `matmul-logits-bindings-v1` and
  `decode-final-logits-matmul-gemv-bindings-v1` capture plans the fast (GPU) logits are read back and compared with serial f32
  (stable) and f64 (reference) evaluations of its weights and hidden state; the
  trigger policy, the routing policy of the execution profile
  (`--numeric-stability-execution-profile`, default the registry's) and the route
  metadata pick accept-fast, prefer-stable (the stable logits are written over
  the fast ones) or abstain (the rest of the stream is skipped and counted in
  `executionSkippedCount`). The next `decode.sample_token` dispatch on
  `sample.wgsl` reading those logits is replayed as a decode boundary (greedy or
  sampled CDF, top-k, top-p; a sampled token is replayed on the host and written
  back, as doe-zig-runtime does). Receipts go to
  `<meta>.numeric-stability.jsonl` and the meta gains `numericStability`, both
  in doe-zig-runtime's format. The receipt `executionIdentity` carries this
  CLI's backend and profile; lane, adapter ordinal, queue family and selection
  policy are null. RMSNorm captures input, weights and output, evaluates serial
  f32/f64 normalization, and uses the immediately following `matmul.logits`
  weights for the decision only when its hidden handle, offset and extent exactly
  match the normalization output; intervening commands and annotated downstream
  operations reject rather than infer mutation effects; stable routing rewrites the normalized tensor, not the decision logits.
  Attention captures query, keys, values and output and preserves the retained
  eight-lane f32 accumulation and serial f64 reference, including score clamping.
  Explicit `numericStability` annotations select buffer spans, candidate row/token
  identities, labels, biases and routing policy; they override auto-detection.
  Unknown capture plans still reject before device work; invalid captures fail
  without publishing a successful receipt. Nonfinite captures, uniforms and
  evaluation results fail closed; implicit captures cannot escape declared buffer
  binding extents. Without a trace meta nothing is
  evaluated by either runtime.
- `output_oracle` schema 1, `sha256_exact_v1`, `zero_fill_v1` initialization:
  with `--validate-output-oracles` the command runs again on isolated
  zero-filled buffers and the read-back target's SHA-256 must equal
  `expected_sha256`; a mismatch exits 1.
- `async_diagnostics` (`pipeline_async_diagnostics`): async shader compilation and
  render pipeline creation, capability queries, standard object reference
  lifetimes, and `full` combinations. `iterations`/`repeat` repeats the selected
  operation. Resource-table/immediates and pixel-local-storage extensions are
  unavailable in the pinned public WebGPU ABI: strict policy fails with
  `diagnostic_extension_unavailable`; explicit `emulate-when-unavailable` records
  the retained public WebGPU fallback's compute/render/bundle operations. These
  diagnostics record without submission and report that path in each trace row.
  Shader receipts cover the actual inline sources compiled. Lifecycle queries use
  an occlusion query set to exercise ownership without enabling an unrequested
  timestamp feature. `check-diagnostics` executes all nine standalone diagnostic
  examples, validates schema/replay, tests strict rejection, and counts actual ABI
  calls for two full iterations (six async pipelines and six command buffers,
  four draws, zero submissions). C and Dawn pass on lavapipe LLVM 21.1.8 CPU Vulkan.
- All seven surface lifecycle commands and retained aliases: create, capabilities,
  configure, acquire, present, unconfigure and release. Linux uses real XCB windows
  (the pinned Dawn build requires an Xlib surface chain on the shared XCB display);
  Darwin owns an NSWindow/CAMetalLayer. Configuration retains format, usage, alpha
  mode, present mode and dimensions. Nonstandard tone mapping and frame-latency
  hints beyond the retained defaults reject before device work. Surface handles
  own acquired texture references through present, reconfigure, unconfigure,
  release or runtime close. Missing/duplicate handles and invalid acquisition or
  presentation order fail explicitly. `check-surface` and `check-surface-dawn`
  require a reachable display; both pass the two surface fixtures plus resize,
  acquired-frame cleanup and state failures on CPU lavapipe/Xwayland. Darwin host
  source passes strict clang syntax checking; native Metal presentation is a
  separate backend prerequisite, not established by that compile.
- `--readback-buffer HANDLE=PATH` (up to 8) writes a buffer's final bytes after
  a successful run, so a test can compare them with bytes computed without the
  GPU.
- Quirks (`src/quirks.c`), as doe-zig-runtime's quirk owner does them: a schema 2
  quirk file (`--quirks`: an array, `[]`, or one object), the matching profile
  `--vendor`/`--api`/`--family`/`--driver` with the Zig defaults
  intel/vulkan/gen12/31.0.101, the same per-command-kind best-quirk score and tie
  rules, and the decision fields (`matched`, scope, class, mode, level, action,
  toggle, score, `matched_count`, `requiresLean`, `blocking`) in each row and its
  hash. Toggle effects come from `config/quirk-toggle-registry.json`, generated
  into the build. `--quirk-mode trace` (the default) records decisions;
  `active` also applies them: `use_temporary_buffer` and the behavioral staging
  toggles route buffer-to-texture and texture-to-texture copies through a staging
  buffer. Render-texture actions follow the platform/lane rules below. Other
  behavioral actions without an implementation fail with exit 2. Omitting
  `--quirks` loads the same embedded sample as the retained runtime.
  No Lean-verified build exists for C, so `lean_required` quirks without a proven
  level are counted as blocking, as in a Zig build without Lean.
- Arguments as listed by `--help`. `--queue-sync-mode per-command|deferred` waits per submission or drains once
  after the command loop; final drain time belongs to the last row and totals.
  `--queue-wait-mode process-events|wait-any` selects real callback delivery
  (default process-events). `--webgpu-ffi-queue-wait-timeout-ns` bounds a queue
  wait; timeout is failure, and its callback storage lives through instance
  destruction. Barriers and host readbacks always complete their required work.
  Partial upload batches also drain before exit; `--gpu-timestamp-mode` admits
  `off`, `auto` (default), and `require`. Compute and render passes use real
  WebGPU timestamp queries and normalized nanosecond readback, excluding warmups
  and isolated output oracles. Auto skips unavailable adapters and deferred queue
  mode; require rejects either condition rather than changing synchronization.
  Query allocation, resolve or readback errors fail the command, not fake a valid
  sample. Timestamp readback adds host overhead but the reported GPU interval is
  strictly pass beginning-to-end; `--api` matches the platform build (`vulkan` on Linux, `metal` on Darwin).
  `--adapter auto|physical|software` is this CLI's own: `auto` takes a
  nonfallback adapter, else the fallback (CPU) adapter.

Every other command kind or field, texture dimension, sample count, aspect, format, oracle
scope, `repeatSynchronization` value, semantic field on another kind, explicit
`numericStability` annotation outside the admitted schema and argument is rejected with exit 2 before
device work.

doe-zig-runtime's native Vulkan lane reports every copy `unsupported` and
continues; this CLI executes them. The row hash chain covers command identity,
so the chains still equal, but `executionStatus` differs on copy rows. Its
render rows report `executionSubmitCount` 0 although the lane submits once;
this CLI reports the one submission, so render execution shapes differ in
that field.

## Explicit public WebGPU renderer

The default remains the native fullscreen proxy described above. A matching
explicit delegate lane selects the retained public WebGPU renderer in
`dawn-c-runtime`: `--backend-lane vulkan_dawn_release` on Linux, or
`metal_dawn_release` on Darwin. Lane aliases normalize as in retained Zig;
a native provider or a lane for another compiled platform rejects before device
work. This route uses the retained textured triangle, vertex buffer, 2x2 sampled
texture, sampler, dynamic uniform binding and depth/stencil attachment.
`encodeMode` selects actual render bundles or passes. Redundant pipeline and
bind-group modes perform the corresponding repeated native calls. Dynamic
offsets select a 12-byte color binding within 64 slots of 256 bytes; invalid
counts, alignment or ranges fail. Missing named color targets are created and
incompatible targets are replaced, as in the public source path.

`make check-public-render DAWN_LIBRARY=/path/to/libwebgpu_dawn.so` with a CPU ICD
checks all eight mode combinations using actual API counters, every rendered
texel against an independent triangle/nearest-sampling oracle, a dynamic offset
that changes shader pixels, four indirect modes, offset rejection, bundle finish
failure/recovery, and the actual delegate CLI route. The meta records the
canonical lane and the embedded public shader's digest; it does not claim the
native proxy shaders ran. The retained Zig delegate executes the reference
stream but its buffer capture is unsupported, so this is an independent pixel
oracle rather than a retained GPU-byte comparison. Darwin is compile-selected;
this new route has been exercised on Linux Dawn/lavapipe only.

## Trace artifacts

Rows follow the trace v1 FNV hash chain (seed `0x9e3779b97f4a7c15`); for the
three streams in `make check` the row hashes equal those `doe-zig-runtime`
(retained runtime/zig source at 796b98338, fresh host trace on 2026-10-25)
wrote with the default embedded quirk. The execution meta carries the Zig meta fields plus `adapterInfo`
naming the executing adapter in the package traces' field names (`vendor`,
`architecture`, `device`, `description`, `backendType`, `adapterType`,
`vendorID`, `deviceID` and `driverVersion`, the last from
`doeNativeAdapterGetPciIdentity`), which bench run receipts read as
`hostIdentity.adapter`, with `module` `doe-c-runtime`,
`executionBackend`/`backendId` `doe_c_vulkan`, `backendSelectionReason`
`fixed_backend` and no `selectionPolicyHash` (there is no lane policy), plus
`hostKernelPrewarmTotalNs`
(kernel pipelines compile before the command loop, outside execution timing, as
doe-zig-runtime prewarms them), `pipelineCache` `disabled`/`platform-unsupported`
(no persistent pipeline cache exists here), and for schema 1 oracles
`outputOracleReferenceClass` `independent_v1` with zero tolerance counts. The
semantic counts (`semanticTracingEnabled`, `semanticOpRowCount`,
`semanticCaptureCount`, `semanticReproCount`) and, when records exist,
`operatorRecordManifestPath` and its SHA-256 `operatorRecordManifestHash` follow
doe-zig-runtime; the manifest bytes use its layout (`[\n`, records joined by
`,\n`, `\n]\n`), so an empty manifest is `[\n\n]\n`. `numericStability`
appears when a receipt was written. `shaderSourceReceipts` lists
every shader the stream compiles (kernel_dispatch kernels in first-use order,
then the render proxy's modules) with its resolved path, entry point, byte
length and SHA-256, and `shaderSourceReceiptsHash` is the SHA-256 of that
array's compact JSON as written; strict comparison between `doe_c_vulkan` and
`dawn_direct_vulkan` requires the two hashes to be equal. Like doe-zig-runtime, the meta
carries no upload arguments: the bench records them per sample, and a meta
`uploadSubmitEvery` would change its upload normalization for this CLI only.
`make check` validates every row and meta against `config/trace.schema.json`
and `config/trace-meta.schema.json`, and the schema gate checks the retained
`examples/doe-c-runtime-*.sample.json` rows and metas.

No shader artifact manifest is written. The strict Dawn-versus-Doe obligation
requires the executed SPIR-V to equal the checked-in `bench/kernels/<name>.spv`,
which doe-zig-runtime executes directly; the C library compiles the WGSL with the
C compiler instead (workgroup_atomic: C SPIR-V `ee68b9ed...`, checked-in
`066ee154...`) and exposes neither SPIR-V module input nor its executed SPIR-V,
so a truthful manifest could not satisfy the obligation.

## Plan mode

`--plan PLAN --workload ID --trace-jsonl ROWS --trace-meta META` runs a direct
execution plan (`bench/plans/generated/*.plan.json`) as doe-plan-executor
(`runtime/zig/src/plan`) lowers it: `buffer_write`, `buffer_load` (the
`<cacheNamespace>/<cacheKey>.bin` asset of exactly `byteLength` bytes under
`DOE_BENCH_ASSET_CACHE_DIR`, else `~/.cache/doe/bench_synthetic_assets`;
`bench/tools/materialize_plan_assets.py` writes them) and `kernel_dispatch`
with buffer bindings. The plan's counts must match its commands and its
`workloadId` the `--workload`. Every row carries the plan step identity
(`semanticOpId` `step-NNNNNN`, stage `runtime_plan`, the command kind as phase,
`planSha256` as execution plan hash) in doe-plan-executor's row layout (`opCode`
is the command label, no quirk fields), and the meta adds `hostPlanArtifactPath`
and `hostPlanArtifactHash` (the plan's `planSha256`) with `quirkMode` `off`.
Kernels resolve under `--kernel-root`, else the IR's `shared.kernelRoot`, else
`bench/inference-pipeline/kernels`, and compile inside the rows (no prewarm),
as doe-plan-executor does. A dispatch's compatibility-stream `semantic*`,
`capture*` and `output_oracle` fields are replaced by the plan identity, as in
doe-plan-executor, and reported on stderr. Quirk, repeat, oracle, numeric
stability and `--trace` arguments are rejected; `--readback-buffer` is admitted.
No operator records are written. Unknown plan fields and kinds are rejected.

## Over Dawn (`dawn-c-runtime`)

The CLI uses only `webgpu.h` plus one optional Doe extension
(`doeNativeAdapterGetPciIdentity`, weak; driverVersion is 0 without it), so the
same sources link against Dawn's `libwebgpu_dawn`: `make dawn
DAWN_LIBRARY=<path>` (default `bench/vendor/dawn/out/Release/libwebgpu_dawn.so`)
builds `$(BUILD)/dawn-c-runtime`, whose rows and meta name module
`dawn-c-runtime` and executionBackend `dawn_direct_vulkan`. It is the C program
over plain `webgpu.h` that the stage-6 dispositions name for
`webgpu-plan-executor` and the Dawn-delegate lane. `make check-dawn` runs the
whole CLI suite (host references, Zig hash chains, plan mode, schemas) over Dawn.
`make bench-bin-dawn` installs the gitignored `bin/dawn-c-runtime` that the
bench executor `dawn_direct_vulkan` (product `dawn-c`) runs with
`LD_LIBRARY_PATH=bench/vendor/dawn/out/Release`, and `dawn_direct_plan_vulkan`
runs its plan mode at the plan boundary; the provider registry lists it
with productRole `dawn`, so its traces are Dawn execution in every gate and
never Doe, and stay distinct from the Zig runtime's `dawn_delegate`.

## Bench comparisons

[`config/doe-runtime-providers.json`](../../../config/doe-runtime-providers.json)
registers this CLI as its own Doe provider (`runtimeImplementation` `c`), so the
compare and claim gates treat its traces as Doe execution while run receipts and
compare reports keep `doe_c_vulkan` and `runtimeImplementation` distinct from
the Zig runtime. `make bench-bin` installs the gitignored
`bin/doe-c-runtime` that the `doe_c_direct_vulkan` executor runs:

```bash
make -C runtime/c/runtime-cli bench-bin
python3 bench/cli.py run --product doe-c --executor-id doe_c_direct_vulkan \
  --workloads bench/workloads/workloads.amd.vulkan.smoke.json --workload-id compute_workgroup_atomic_1024
```

The executor passes the workload's `--quirks` in the default trace mode, so its
rows carry the same quirk decisions as doe-zig-runtime's. `doe_c_direct_plan_vulkan`
runs plan mode at the plan boundary with doe-plan-executor's arguments (no
backend lane); like doe-plan-executor it gets no strict oracle preflight.
Workloads outside the admitted input (custom render pipelines,
textures) fail the run rather than being skipped. The Dawn-versus-Doe strict
Vulkan shader-artifact obligation applies to C traces too; the CLI writes no
shader artifact manifest, so that obligation fails for them.

## Recorded runs

`make check` on lavapipe (llvmpipe, Vulkan CPU adapter), 2026-10-25, source
71c285233 plus this owner: `p0_resource_lifecycle`, `rmsnorm_tree` and
`workgroup_atomic` exit 0, pass `pipeline/trace/replay.py`, match the
doe-zig-runtime hash chains, and the `workgroup_atomic` oracle matches its
expected digest; `--command-repeat 2`, an oracle mismatch and five rejections
behave as specified. The same run on source 0c056f1de plus the copy and texture
commands executes five copy example streams and a roundtrip of copies in every
direction and a texture write whose three read-back buffers equal bytes computed
on the host, with hash chains equal to doe-zig-runtime's, and rejects or fails
an invalid row pitch, a mip-level copy, a mismatched query and a missing
read-back handle. With render and sampler commands, six render example streams
(direct, indexed, state-bound, viewport/scissor, sampler/texture/render and
upload/render) match doe-zig-runtime's hash chains, a scissored draw and an
indexed draw read back as the proxy color inside the scissor and the clear color
outside, and a missing render target fails. With semantic identity (base
977122f33, 2026-10-25), a workgroup_atomic stream carrying every semantic field
has row hashes equal to doe-zig-runtime's executed rows, operator records equal
to its records outside `execution` and `shaderArtifacts` (checked against
`config/operator-execution-record.schema.json`), repro command files equal byte
for byte, and captures equal to its captures and to the
`bench/oracles/workgroup_recurrence_u32_v1.py` host reference; without a trace
meta the greedy decode stream's rows equal doe-zig-runtime's trace-only rows.
Numeric stability (2026-10-25): doe-zig-runtime crashes inside llvmpipe executing
`bench/inference-pipeline/kernels/matmul-gemv.spv`, so its reference receipts
come from a guarded Radeon run (`tests/fixtures/zig-radeon-numeric-stability-*.jsonl`,
HEAD 71574e529). On lavapipe the greedy and sampled receipts of `doe-c-runtime`
and `dawn-c-runtime` are byte-equal to them outside `executionIdentity`
(candidates, digests, layout fingerprint, trigger, route, decode boundary), and
the summaries are equal; host tests cover the prefer-stable rewrite, abstain stop
and accept-fast routes. Plan mode (base 8cdc66317, 2026-10-25): rows for
`inference_gemma3_270m_decode_1tok` (37) and
`inference_gemma3_270m_prefill_64tok_decode_64tok` (1583) have hashes equal to
doe-plan-executor `--dry-run` rows (the hash does not depend on execution) and
every row executes (18 and 1170 dispatches). doe-plan-executor SIGSEGVs inside
llvmpipe's libLLVM executing `inference_gemma3_270m_decode_1tok`, so execution
results are not compared with Zig. `make check` runs a small plan whose load and dispatches read
back as the host computes them, and `bench/cli.py run --product doe-c
--executor-id doe_c_direct_plan_vulkan` completes the prefill/decode workload.
Over Dawn (base fba5980ac + this owner; Dawn library in
`bench/vendor/dawn/out/Release`, vendor checkout d04f88359, built 2026-10-15;
lavapipe): `make check-dawn` passes; the
prefill/decode plan runs all 1583 rows and its final logits (buffer 2227, 4096
f32) and token (buffer 2228, 1664) are bit-identical to `doe-c-runtime` over
`libwebgpu_doe_c`. Bench lane (base 71574e529, lavapipe, 7 iterations):
`compute_workgroup_atomic_1024` through `doe_c_direct_vulkan` against
`dawn_direct_vulkan` is strict/operation comparable with equal shader source
receipts (Dawn p50 4.4% faster on the CPU adapter, not claimable); against the
Zig `dawn_delegate_vulkan` lane the same C run is diagnostic, blocked by the
native SPIR-V artifact obligation, because this CLI writes no shader artifact
manifest. Plan lane (base 6a95fb73e, lavapipe, 7 iterations):
`inference_gemma3_270m_prefill_64tok_decode_64tok` through
`doe_c_direct_plan_vulkan` against `dawn_direct_plan_vulkan` is strict/operation
comparable with equal shader source receipts and passes the structural gate
(Dawn p50 924 ms vs C 1405 ms on the CPU adapter, not claimable); every
sample's 1583 rows on both sides hash-equal doe-plan-executor `--dry-run` rows.
`pipeline/trace/replay.py` rejects plan rows (`opCode` `buffer_write`) from
either runtime, so the trace gate does not apply to plan lanes yet. Dispatch
kinds (base 6a95fb73e): the eight `dispatch`/`dispatch_indirect` examples and a
grid stream reusing a workgroup_atomic pipeline match doe-zig-runtime's row
hashes, the reuse stream reads back as the host oracle's four dispatches, and
on scratch comparable contracts (`compute_dispatch_grid`,
`compute_dispatch_indirect_live_args`, an independent `rmsnorm_tree`) C against
Dawn is strict/operation comparable with equal shader source receipts. The
committed contracts keep those dispatch rows directional, and the three
`compute_matvec_32768x2048_*` rows bind 256 MiB, beyond lavapipe's 128 MiB
`maxStorageBufferRange`, so they fail on lavapipe in both CLIs. A CPU
adapter run is not GPU evidence, a performance result
or bench comparability; the physical surface check below is separate.

The 2026-10-25 mapping continuation exercises eight mapping example streams,
repeated maps and mode/size changes through both C and Dawn on llvmpipe
(LLVM 21.1.8, CPU Vulkan). `test_map` checks partial zero-fill, reuse,
replacement and final unmap. `test_texture_cleanup` counts actual view
acquisition/release when a later buffer binding fails for missing size,
overflow or injected allocation failure. These checks do not establish
physical-backend qualification or performance advantage.


The original AMD Vulkan workload inventory admits all 84 workload rows
(81 unique command paths). The expanded host-only inventory admits 91 of all 98 tracked
`examples/*_commands.json` files, including every kind in Zig's command-kind
registry. The other seven are inference `buffer_load` compatibility streams:
Zig's raw command parser also rejects that kind; their active consumers use
normalized `planPath` through the plan executor. Parser admission is distinct
from execution: for example `copy_protocol` has a row pitch of 8 that the public
WebGPU path correctly rejects (the native Zig CLI marks copies unsupported).


Metal builds emit `doe_c_metal` or `dawn_c_metal`, distinct from the retained
`dawn_direct_metal` plan-executor identity. The bench routes
`doe_c_direct_metal`, `doe_c_direct_plan_metal`, `dawn_c_direct_metal` and
`dawn_c_direct_plan_metal` select C binaries at their declared command/plan
boundaries; provider classification comes from `config/doe-runtime-providers.json`.
Windows C routes `doe_c_direct_d3d12` and `doe_c_direct_plan_d3d12` select
`doe-c-runtime.exe` with identity `doe_c_d3d12`; no backend-policy selection or
comparability is inferred. Stage the SDK builder output together with its DLLs
in this owner's ignored `bin/` directory to use these explicit routes.

The 2026-10-25 focused macOS 26.6.2 (25G83) check compiled/linked the entire C
CLI with Clang and exercised the actual Apple M3/Metal adapter: retained handle
300 and 41001 surface fixtures, BGRA presentation, 64x64 to 96x48 resize, missing
handle rejection and double-acquire rejection. The native Metal owner fixed a
presentation retirement error exposed by the resize consumer. CAMetalLayer's
advertised BGRA formats are honored; the retained default RGBA format correctly
fails on that platform. This is focused physical execution, not broad hardware
qualification or a performance result. The Darwin build enables the Darwin SDK
feature definitions required by AppKit alongside the POSIX API definitions.

Active `use_temporary_render_texture` and the
`MetalRenderR8RG8UnormSmallMipToTempTexture` toggle preserve retained kind filtering
(render_draw/render_pass only). The public renderer stages R8/RG8 at mip0 only
when `minMipLevel` is zero; the toggle's default minimum is two. It uses the
retained wrapping temporary-handle offset and encodes copy-back before submit.
Native Metal stages every flagged draw, matching its retained implementation;
native Vulkan/D3D12 preserve the retained flag's lack of an encoding effect.
Focused Dawn/lavapipe controls count actual copy calls and compare every output
texel across affected formats, unaffected RGBA, and minimum-mip guards. The
actual CLI also reads both the original and temporary textures after an active
quirk. A focused actual Darwin build and Apple M3 run on 2026-10-25 also
executed one native Metal active RGBA draw with `minMipLevel=2`, checking all
32x32 output pixels and row padding under an external five-second process
limit. CLI SHA-256 was
`7e51a1918000fa6f257e91cbccb9d3e71dc7094f2f6bf246f4ece39f9bd058a1`;
native library SHA-256 was
`ea55ffb066968f82163230eb4faffc4e83ffd50e2c95b15b6cb6d751ee0ba7f5`.
This is a focused render/copy consumer check, not hardware qualification.

The ordinary `matmul-logits-bindings-v1` numeric capture plan shares the
retained serial f32/f64 evaluation and receipt/routing owner with decode logits,
using hidden binding1 and weights binding2. Host controls cover stable rewrite,
abstention and observe-only routes with independently calculated logits. The
unchanged `matmul_logits_forward_f16accum.wgsl` bounded 2x3 consumer now executes
on the native C Vulkan path with a software llvmpipe CPU adapter. Inputs
`hidden=[1,1,1]`, `weights=[2048,1,-2048,0,.25,0]` produce actual half-accumulated
fast logits `[0,.25]`, stable/reference `[1,.25]`, and preferred-stable device
readback `[1,.25]`. This is software-Vulkan execution, not physical GPU
qualification. The runtime requests ShaderF16 only when the adapter advertises it.
