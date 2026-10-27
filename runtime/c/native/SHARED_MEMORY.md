# Retained Metal shared-memory compatibility

`include/doe_native_shared_memory.h` and `SHARED_MEMORY_SYMBOLS.txt` preserve the
25 shared fence/memory entrypoints in
`runtime/zig/src/dropin/dropin_browser_shared_memory.zig`. The procedure table
also resolves the 22 names that the retained provider exposes only dynamically.
This is an additive compatibility boundary, not a new feature advertisement or
production-provider switch.

Browser-facing descriptor layouts match Chromium `4a6fb385` / Dawn `758e5e6a`,
including independent fence/value counts, buffer initialization and usage/size
field order. Begin requires equal fence and value spans before reserving access
or retaining inputs. End publishes both counts; FreeMembers frees each owned
array independently, including a values-only output. These representation fixes
do not expand the shared-image policy described below.

## Implemented path and ownership

- A Metal device imports a retained `MTLSharedEvent` or a single-plane
  `IOSurface`. Import failures leave no published object. The original native
  handle, channel order and device identity are preserved.
- Import inspects the IOSurface's physical packed RGBA8/BGRA8 unorm format and
  dimensions. `GetProperties` publishes them before any texture exists;
  unsupported pixel layouts fail import without retaining the surface.
- Shared texture creation requires that physical format, exact 2D surface
  dimensions, one mip, one layer and one sample. Compatible sRGB formats are
  declared as view formats and do not change the shared memory's properties. The existing
  resource owner validates descriptors and owns ordinary textures, views and
  command references. The Metal external-plane adopter preserves producer
  contents instead of clearing the imported allocation.
- Each texture retains its shared-memory provenance. Begin/End require that
  exact memory/texture association, and End requires the same texture as Begin.
  A weak active identity avoids a reference cycle; the resource finalizer clears
  it only when that exact published texture disappears.
- Begin snapshots and retains all input fences, reserves the access state, then
  waits for native producer values **without a device mutex held**. Concurrent
  Begin/End cannot observe an access as active before those waits succeed.
- End with an output state creates a fresh real shared event and records its
  signal after preceding queue work. No pre-access queue timeline value can be
  reused. The existing pending-command owner retains the signal event and device
  through unknown completion. Failed End leaves both access and caller output
  unchanged; the original native operation/code/detail survives rollback.
- Fence export returns the original borrowed native event identity. Output-state
  free-members releases each fence and both owned arrays. Imported memory and
  fences retain their device independently of public device/queue references.

## Deliberately retained limits

This is **not** all Dawn shared-image policy. Like the retained implementation,
Begin ignores `initialized`, `concurrentRead` and its extension chain: there is one
exclusive access state, not concurrent readers or undefined-content tracking.
Producer synchronization comes from the supplied fence/value pairs. End with a
null state only closes access; it does not submit a completion signal. Successful
End with a state reports `initialized=true`, as retained. These fields do not
establish zero-initialization semantics for arbitrary shared allocations.

`allowStorageBinding` remains ABI-compatible but does not enable storage usage.
Only CopySrc, CopyDst, TextureBinding and RenderAttachment are admitted. Unlike
retained loose handle checks, unrelated textures cannot acquire/end this memory's
access, failed texture creation cannot clear another access reservation, and
IsDeviceLost observes the actual retained device state.

All shared-buffer entrypoints remain explicitly unsupported: import/create return
null, access/property queries return Error, IsDeviceLost returns true, and the
retained cleanup/label operations have no created object to act on. Vulkan,
D3D12, multi-plane shared surfaces and alternate native fence types are not
silently substituted. The shared APIs do not advertise new requestable features.

Ordinary external-texture planes also permit genuine Metal CopySrc blits, using
the same native plane identity. This does not imply YUV or color conversion.

## Focused evidence

- `make -C runtime/c/native check-shared-memory`: public ABI over real resource,
  device and pending owners with private Metal call ledgers. Covers exact
  provenance, sRGB channel order, no import zero-overwrite, producer lock
  exclusion, early public fence/texture release, failure during a blocked Begin,
  fresh completion signals, first-error detail, unknown-work retention and
  final cleanup. No GPU results are synthesized.
- `make -C runtime/c/native check-iosurface-metadata` on Darwin: actual BGRA/RGBA
  IOSurface import, properties before texture creation, exact-format rejection,
  compatible sRGB views and early native/public releases. This selects physical
  Metal but submits no GPU work.
- `make -C runtime/c/backends/metal check`: existing ownership/native-call tests;
  external typed-call controls include CF retain/release, single-plane admission,
  shared format/usage arguments and event-value reads.
- `make -C runtime/c/backends/metal abi-compile`: host typed-call compilation for
  the arm64/x86_64 branches, not Apple SDK verification.
- `make -C runtime/c/backends/metal sdk-abi`: actual Apple SDK comparison, including
  CFTypeRef/event calls; requires an installed Apple SDK.

The metadata consumer passes on Apple M3. Native event synchronization, GPU
execution of imported contents and Fawn browser external-image acceptance remain
separate gates. Metadata, symbol resolution and compilation do not qualify them.
