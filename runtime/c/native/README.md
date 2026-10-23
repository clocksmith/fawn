# Experimental C public WebGPU native frontend

Status: authored, strict-compiled and developer-exercised through the pinned
public `webgpu.h`. Not independently qualified, a complete WebGPU replacement,
a performance result or physical-GPU evidence. Production/default selection is
unchanged. Only the explicitly implemented symbols in `src/procedures.c` are
exported; unknown and unimplemented names return NULL from `wgpuGetProcAddress`.

## Actual ownership and execution path

- Public instances alias the accepted C instance, including its future queue.
  Requests, map, queue, loss, compilation-info and async pipeline callbacks use
  accepted C callback payloads and leases. Device-loss obligations outlive device
  storage without retaining a device cycle; last external release clears the weak
  callback handle before destruction. Explicit destruction completes loss once.
- Adapters/devices have their sole native references here. Queue handles retain
  their device. Public buffers, shaders, layouts, groups, pipelines, encoders,
  passes and command buffers alias their typed C resource owners, not wrappers
  with duplicate references. Device-bound shader resources adopt the separately
  shareable immutable `doe_shader` artifact.
- Shader creation calls the C compiler. Entry selection uses the actual entry
  reflection and its static resource closure. Layout checks include reflected
  minimum binding sizes, access kind, stage, resource/device identity and native
  limits. The capability scanner admits Shader/Matrix, baseline 1D images and
  ImageQuery explicitly; other capabilities/extensions reject before pipeline
  creation. Extended storage formats and cube arrays remain disabled. Workgroup
  storage and dimensions use real backend limits.
- Compiler `src/spirv.c` owns guarded loads/stores: runtime array bounds use
  `OpArrayLength`, signed indices have a nonnegative check, invalid loads return
  zero and invalid stores do not execute. The Vulkan context also requires and
  enables robustBufferAccess. The frontend does not recreate source analysis or
  fabricate retained TranslationInfo extent/texture metadata. ABI6 owns guarded
  integer image coordinates/layers/mips/samples, with zero loads and discarded
  stores outside bounds. Native admission maps semantic format names, never
  retained model enum numbers.
- Resource commands retain their dependencies, seal on finish, validate map and
  destroyed state before single-use submit, and hold submission references until
  settlement. Vulkan commands separately own the actual native recording and
  completion obligations. Buffer mapping stages real coherent native contents;
  writable maps write back on unmap. No CPU compute emulation is used.
- Public textures/views/samplers alias the same resource owner. Images are fully
  zero-initialized at every mip/layer through actual native uploads before public
  publication, including images without public CopyDst usage. Private native
  initialization commands use the same pending-submission owner and an explicit
  submission device lease. Packed queue writes and 256-byte-aligned public command
  copies retain their distinct canonical validation. Native views/samplers remain
  alive through the recorded resource and backend obligations.
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

Graphics, surfaces, query/timestamp entrypoints and indirect dispatch remain
absent. Texture format reinterpretation, depth/multisampled images, cube arrays
and anisotropic sampling are rejected. The baseline admits 18 named Vulkan
color formats, subject to actual device usage/format capabilities. Device
descriptors requesting required features or required limits reject rather than
silently succeeding. Buffer, sampled/storage image and sampler layouts are
admitted; extensions, binding arrays,
immediates, specialization constants and nonbaseline shader capabilities reject.
ABI6 lacks exact texture/sampler pair-use reflection through helper parameters.
Selected layouts combining UnfilterableFloat with Filtering samplers therefore
reject explicitly; this is an unsupported analysis case, not full admission parity.
The image receipt's dependency checkpoint predates canonical cross-binding usage
scope validation. That identified admission gap is being corrected in the resource
owner and requires a separate integrated native check; these image results do not
qualify conflicting writable aliases.
Descriptor labels and SetLabel use the existing label owner, including exact
length-delimited bytes and resource-finalizer cleanup. Failed WGSL modules retain
real compiler diagnostics for GetCompilationInfo. Compute pipeline construction
is synchronous; the async API delivers its actual result through the requested
callback mode, not a claimed background compiler. No blanket success stubs or
fallback provider resolution exist. The public advertised limits
cover the implemented compute/image path only and do not claim the WebGPU Core
feature level is complete; adapter/device creation here is an experimental compute consumer path.

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
PASS device loss modes/future/last-external-release/failed-creation, owned labels, async pipeline and real compilation diagnostics
PASS public textures: actual zero mips, packed upload, linear sampling, image OOB guards, storage readback, early releases and filter/pair/copy negatives (CPU Vulkan only)
PASS every implemented procedure address matches; every unprovided name returns NULL
PASS exact exports: 114 implemented, 88 pinned unprovided, no private exports
```

A compute shader with two declared entries selects `transform`, runs 72
invocations on a 64-element runtime array, and checks all 64 readback values.
Checks also cover mapped-at-creation writeback, dynamic offsets, invalid dynamic
offset poisoning, readonly-map write rejection, wrong-device shader rejection,
one-shot command rejection, cancellation generation, duplicate wait records,
early caller releases and explicit recursive event pumping. C and C++ invoke
real returned procedures through the pinned typed function pointers. Lifecycle
checks cover all three loss modes, eight concurrent last-reference releases,
failed device creation, nested event pumping, label copying/removal, deferred
shader-release-safe diagnostics and success/failure async pipeline delivery. A
borrowed callback handle can acquire a new external reference after concurrent
last external release, without restoring destroyed state or weak loss identity.
Image checks execute filtering and storage instructions, robust OOB handling,
zeroed mips, packed uploads, readback and early releases; negatives cover float32
filterability, unresolved sampler pairing, destroyed views and copy-row alignment.

```
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json \
make -C runtime/c/native BUILD=/tmp/doe-c-native-asan \
  SANITIZE='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check
```

That build instruments both C compiler and native runtime owners. LeakSanitizer
is separately unavailable in this execution environment under ptrace; disabling
leak detection does not claim leak qualification. `DEVELOPER_RECEIPT_IMAGES.json`
binds the image increment to immutable committed dependency8a3fd4c1 plus listed
native bytes, including a fresh canonical compiler build. An earlier live image
sanitizer build overlapped unfinished ABI7 compiler edits and is not source-bound
evidence. `DEVELOPER_RECEIPT_LIFECYCLE.json` binds the preceding lifecycle work. The earlier
`DEVELOPER_RECEIPT.json` and source manifest remain the historical first compute
checkpoint, not evidence for changed bytes. An initial live-worktree build caught
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
