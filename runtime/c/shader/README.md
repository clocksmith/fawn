# Owned C shader results

Authored and focused-host-checked, consumed by the experimental C-only native
compute path, not independently or physically qualified.
This is the persistent ownership half of the standalone C compiler boundary:
one compilation pipeline, copied source/compiler identity, exact immutable target,
types, resource layouts, sparse texture/sampler descriptors, graphics IO leaves
and separate entry closures. ABI6 handle and stage interface descriptors are retained
in the same owned allocation,
not represented as fabricated buffer layouts. Static-library consumers rebuild
with this internal view header; the retained versioned compiler records do not grow. It follows the allocation,
clone/deinit and entry-binding responsibility of the retained
[TranslationInfo owner](../../zig/src/compiler/wgsl/runtime/runtime_translation_info.zig),
without pretending that standalone reflection supplies its dispatch/texture
preconditions, Metal size-buffer policy or complete production cache semantics.

The caller supplies explicit compiler capacities and allocator hooks. Additional
interface-attribute and stage-interface capacities are explicit options; zero
retains compute-only admission rather than inventing hidden capacity. Graphics
entries retain actual stage and zero compute metadata. Two compiler
calls measure then publish; no source re-parser or alternate semantic engine.
The temporary workspace is released before publication. One owned allocation
contains the retained result. Identity matching compares exact source, explicit
compiler identity and entry selection, not hashes alone. It is not a cache.
Inputs/output/error spans must be valid, disjoint and stable through the call;
allocator hooks return fresh suitably aligned storage and may not mutate inputs.
Retained views require a live reference. No GPU or device lifetime is acquired.

Build the C compiler first, then run (paths may be absolute):

```sh
make -C runtime/c/shader test BUILD=/tmp/doe-shader \
  COMPILER_LIBRARY=/path/to/libdoe_compiler.so
make -C runtime/c/shader sanitize BUILD=/tmp/doe-shader \
  COMPILER_LIBRARY=/path/to/libdoe_compiler.so
```

The focused test covers two entry closures, exact identity, copied source,
allocation failure at both acquisition points for buffer, image and graphics modules, error
cleanup and retained views, flat IO leaves and per-entry stage identity. Compilation
is not device execution.
Sanitizers instrument this owner; the selected compiler library has its own
instrumentation status. LeakSanitizer is disabled in the ptrace cloud environment;
allocation counters check this owner's cleanup, not universal leak freedom.
The first local test exposed an adapter mistake: ABI5 measurement requires a NULL
output descriptor. That was corrected without changing compiler semantics.

`make test-vulkan-images BUILD=/tmp/doe-shader COMPILER_LIBRARY=/path/to/libdoe_compiler.so`
is an optional real backend consumer of newly compiled WGSL, not a prebuilt shader.
It checks texture dimensions, integer loads, level-zero sampling at reversed
coordinates, storage-image writes, 72 invocations into 64 pixels, and early caller
release against a 256-byte CPU result. The image and sampler metadata remain owned
through pipeline creation. The target uses the Vulkan owner's canonical sources.
Software adapters require explicit `NATIVE_ARGS=--allow-software`; select an ICD
only in that process environment. A software pass does not qualify physical GPU
execution or the not-yet-integrated public WebGPU texture APIs.

`make test-vulkan-graphics BUILD=/tmp/doe-shader COMPILER_LIBRARY=/path/to/libdoe_compiler.so`
compiles real WGSL vertex and fragment entries, retaining their typed IO and target
in the same owned artifact. It renders an asymmetric top-half rectangle with a flat
integer varying and checks all256 readback bytes after early shader/vertex/view/
pipeline releases. Explicit WebGPU viewport mapping belongs to the Vulkan backend;
the compiler leaves source position unchanged. Software opt-in is the same as the
image target. This is a single-color offscreen developer integration, not full
render-pipeline validation, public graphics API, interstage device-limit or physical
qualification. No retained Tint binary is substituted for either stage.

Entryless WGSL modules are valid owned artifacts: NONE selection preserves their
validated metadata but yields zero words and entries, not a fabricated Vulkan
shader. Pipeline creation still requires a real selected entry. Matrix records
use explicit ABI8 column/row/stride metadata; the owner copies it without a
second layout calculation. Empty/library/matrix controls exercise both acquisition
failures, exact source identity, retained views and final release.

Add `NATIVE_ARGS="--allow-software --demote"` to the graphics consumer for
explicit optional demote feature selection and an asymmetric discard-pixel oracle.
The baseline and demote modes pass on SwiftShader with early caller releases;
this does not qualify physical hardware, derivatives or helper-invocation parity.
An unavailable requested feature reports SKIP77 rather than substituting OpKill.

The internal owned artifact selects explicit compiler ABI9. Atomic records retain
their referenced scalar type and non-constructible flag; they are not ordinary
scalar values. Buffer layout remains the compiler owner’s result. Both acquisition
faults and retained-view lifetime are checked for atomic-containing modules.
