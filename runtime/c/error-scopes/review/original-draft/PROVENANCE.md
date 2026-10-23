# Source trace and disposition

Observed repository: /workspace/shared/doe-c-migration-20261023.
Observed branch experimental/c-migration-dot-20261023 and HEAD
da9b75a09e93248f52749d5aa7dd414f4d8a0016. Common guide was published at
03d7a5cc311c46f0fa5df01f4e3383d126e3253c. Parent restricted this pass to /tmp;
no repository edit, build, heavy test, dependency installation or publication.
All paths below are repository-relative. SOURCE_INPUTS.json contains byte hashes,
line counts, tracked HEAD comparisons and source-backed call-site snippets.

Direct owner: runtime/zig/src/runtime/diagnostics/error_scope.zig, 260 lines at
inspection (manifest is authoritative). Its stack portion is proposed C-owned;
classification, constants, callback declarations and ScopedError compatibility
remain explicitly retained. Tests: runtime/zig/tests/core/error_scope_test.zig.
Existing tests cover only part of the concurrency/reentry/filter-edge obligation.

Lifecycle: native/support/doe_native_object_types.zig embeds one stack and supplies
requireAlive. native/lifecycle/doe_instance_device_native.zig creates the final
stable object, clears the handler at logical destroy and frees at final release.
Object allocation/refcount helpers remain native/support/doe_native_object_helpers.zig.
Shared build/source-layout/inventory files already contain unrelated dirty work;
they are observations, not permission to edit or exact-seal acceptance inputs yet.

Six shared classifier calls:
- native/surface/doe_surface_native.zig: configuration errors, with two overrides.
- native/compute/doe_compute_fast.zig: construction errors, with two overrides.
- native/render/doe_bundle_native.zig: recording-contract failure diagnostics.
- native/command/doe_command_recording.zig: first encoder-state failure only.
- native/resource/doe_bind_group_native.zig: unsupported immediate layout.
- native/vulkan/vulkan_render_pipeline_native.zig: shader retention failures.
Paths above are relative to runtime/zig/src. Complete direct .error_scopes producer
rows, including native resource/queue/render/compute/backend paths, are enumerated
in SOURCE_INPUTS.json; no extra error classifier was inferred from filenames.

Native exports: native/diagnostics/doe_error_scope_native.zig -> API core facade ->
mod/native facade and actual drop-in proc registration. Public standard pop is
in dropin/wgpu_dropin_ext_a_exports.zig: ScopeReply copies the synchronous result,
retains actual device ownership and enters the separately owned callback/future
path. callbacks/CONTRACT.md and instance/CONTRACT.md define that boundary; they
are not rewritten by this design. Public callback delivery does not move here.

Actual external users include runtime/bridge/webgpu-addon/doe_napi_{caps,helpers,
internal,globals} and packages/doe-gpu/src/vendor/webgpu/{index,bun-ffi}. They remain
source-traced but unqualified. The Node addon prefers standard pop when available but shares its
native callback declaration even though standard pop adds status: this inherited
problem requires separate repair/qualification, not a wider claim here. Bun's
flattened ABI assumptions likewise are not validated by this read-only design.
Browser WebGPU uses its own provider; no browser success is Doe adoption evidence.

Pinned upstream ABI authority: config/webgpu-abi-source.json,
runtime/zig/vendor/webgpu-headers/webgpu.h and generated/webgpu_upstream.zig.
Doe's native callback is not either standard upstream callback type. The C owner
may reuse the actual WGPUStringView declaration, but must not copy or replace it.
Top-level source license is MIT, Copyright (c) 2026 Doe contributors. Retain source
attribution and upstream header LICENSE; no implementation code from an oracle,
new dependency or relabelled upstream license is proposed.

The private typed bridge mirrors the admitted pattern in
runtime/c/futures/integration/doe_futures_zig_bridge.h and
runtime/zig/src/native/support/doe_future_c_adapter.zig. The new owner does not
copy the queue, its state machine or reference policy. New synchronization means
new init/destroy failure gates, not inherited future-owner acceptance.

Instruction/authority inspection included AGENTS, GOALS, INTENT, root/runtime/C/
Zig charters, runtime/c/STYLE.md, futures/STYLE.md, runtime map and native-adapter
subset, MIGRATION, QUALITY_TARGET, public callback/device-lifecycle contracts,
C callback/instance contracts, source and actual consumers. No test receipts were
rewritten; no full-C completion, production-default or physical claim is made.
