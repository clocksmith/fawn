# Experimental C public WebGPU compute frontend

Status: authored, strict-compiled and developer-exercised through the pinned
public `webgpu.h`. Not independently qualified, a complete WebGPU replacement,
a performance result or physical-GPU evidence. Production/default selection is
unchanged. Only the explicitly implemented symbols in `src/procedures.c` are
exported; unknown and unimplemented names return NULL from `wgpuGetProcAddress`.

## Actual ownership and execution path

- Public instances alias the accepted C instance, including its future queue.
  Requests, map and queue callbacks use accepted C callback payloads and leases.
- Adapters/devices have their sole native references here. Queue handles retain
  their device. Public buffers, shaders, layouts, groups, pipelines, encoders,
  passes and command buffers alias their typed C resource owners, not wrappers
  with duplicate references. Device-bound shader resources adopt the separately
  shareable immutable `doe_shader` artifact.
- Shader creation calls compiler ABI5. Entry selection uses the actual entry
  reflection and its static resource closure. Layout checks include reflected
  minimum binding sizes, access kind, stage, resource/device identity and native
  limits. SPIR-V capabilities beyond baseline Shader/Matrix and extensions are
  rejected before backend pipeline creation. Workgroup storage and dimensions
  use real backend limits.
- Compiler `src/spirv.c` owns guarded loads/stores: runtime array bounds use
  `OpArrayLength`, signed indices have a nonnegative check, invalid loads return
  zero and invalid stores do not execute. The Vulkan context also requires and
  enables robustBufferAccess. The frontend does not recreate source analysis or
  fabricate retained TranslationInfo extent/texture metadata.
- Resource commands retain their dependencies, seal on finish, validate map and
  destroyed state before single-use submit, and hold submission references until
  settlement. Vulkan commands separately own the actual native recording and
  completion obligations. Buffer mapping stages real coherent native contents;
  writable maps write back on unmap. No CPU compute emulation is used.
- QueueSubmit currently performs a synchronous native wait/recovery. This is not
  a completed asynchronous producer lane or a latency/parity claim. Uncertain
  completion retains both native and resource obligations; later queue/map calls
  may retry recovery. If still unresolved, work-done publishes no future rather
  than reporting success. Releasing all caller handles does not free unresolved
  work; unattended recovery/liveness remains unfinished.
- Device serialization covers mutable resources and Vulkan state. Internal
  recursive acquisition permits resource-finalizer cleanup using the same lock.
  Foreign callbacks run only after unlocking, with required full-call leases.

## Intentionally unprovided or rejected

Graphics, surfaces, public textures/samplers, query/timestamp entrypoints, indirect
dispatch, device-loss future/callback delivery, async pipeline creation, compilation
info and SetLabel exports remain absent. Device descriptors requesting device-loss
callbacks, required features or required limits are rejected rather than silently
accepted. Buffer-only binding layouts are admitted; extensions, binding arrays,
immediates, specialization constants and nonbaseline shader capabilities reject.
Descriptor labels are not retained by this experimental surface. No blanket
success stubs or fallback provider resolution exist. The public advertised limits
are deliberately compute-only and do not claim the WebGPU Core feature level is
complete; adapter/device creation here is an experimental compute consumer path.

## Reproduce from a coherent checkout

The Makefile builds `libdoe_compiler.so` using the compiler owner's canonical
source list and flags, then links `libwebgpu_doe_c.so` from C owners only. It writes
all outputs to /tmp by default. Existing GCC/G++ and Vulkan loader/ICD are needed;
no installation or machine-setting change is performed.

```
make -C runtime/c/native BUILD=/tmp/doe-c-native check \
  VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json
```

The checked-in test requires a fallback CPU adapter and verifies the returned
adapter type is CPU. Its ordinary result is:

```
Explicit software-only WebGPU C path: SwiftShader Device (Subzero); not physical GPU qualification
PASS public ABI: compute/readback, OOB guards, dynamic offsets, cross-device rejection, one-shot submit, early releases, callback reentry and map cancellation
PASS C++ pinned typed procedure calls and unsupported-name rejection
```

A compute shader with two declared entries selects `transform`, runs 72
invocations on a 64-element runtime array, and checks all 64 readback values.
Checks also cover mapped-at-creation writeback, dynamic offsets, invalid dynamic
offset poisoning, readonly-map write rejection, wrong-device shader rejection,
one-shot command rejection, cancellation generation, duplicate wait records,
early caller releases and explicit recursive event pumping. C and C++ invoke
real returned procedures through the pinned typed function pointers.

```
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json \
make -C runtime/c/native BUILD=/tmp/doe-c-native-asan \
  SANITIZE='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check
```

That build instruments both C compiler and native runtime owners. LeakSanitizer
is separately unavailable in this execution environment under ptrace; disabling
leak detection does not claim leak qualification. The developer receipt binds
exact source and artifact hashes. An initial live-worktree compiler build caught
unfinished parallel ABI6 enum edits; checkpoint testing instead materialized the
committed ABI5 compiler 54f0fb9f6f7202cb13d37e7440ea9d7e13e99b0f with the recorded
runtime snapshot. That development drift is not a published checkpoint result.

## Physical hold

Do not run this on Miles or Suby while their possible disassembly/cooling hold
remains. No physical execution was attempted. After explicit clearance, an
independent hardware consumer must request a nonfallback adapter, verify actual
adapter/driver and ICD identity, and capture its own source-bound results. Do not
relabel the software-only fixture as hardware qualification or infer parity from
these developer checks.
