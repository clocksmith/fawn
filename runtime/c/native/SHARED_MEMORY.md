# Shared texture memory and fences

`include/doe_native_shared_memory.h` and `SHARED_MEMORY_SYMBOLS.txt` preserve the
25 shared fence/memory entrypoints in
`runtime/zig/src/dropin/dropin_browser_shared_memory.zig`. The procedure table
also resolves the 22 names that the retained provider exposes only dynamically.
The adapter advertises the pinned Dawn `SharedTextureMemoryIOSurface`
(`0x50024`) only when IOSurface APIs and the selected Metal device's import
selector are available. `SharedFenceMTLSharedEvent` (`0x5002a`) requires a real
event allocated on the adapter context through the existing timeline owner.
Device requests, `HasFeature` and feature enumeration share the native feature
owner's compact mapping; ordinary device defaults do not enable these features.
Adapter probing does not select a device's synchronization policy. The legacy
compatibility exports remain callable as before; this adds truthful capability
disclosure for Dawn's browser import gate, without changing provider defaults.

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
- Begin snapshots and retains all input fences, then records GPU-side waits on an
  ordinary accepted command before first use. Registration never polls or sleeps
  for producer values. The command retains native event leases through retirement,
  including unknown completion. Failed admission preserves access state.
- End exports the real queue timeline point after accepted producer dependencies
  and texture work, without waiting for scheduling or GPU completion. Zero-work
  accesses carry the accepted producer point, including across later zero-work
  accesses. A reserved but uncommitted timeline value is never exported. Devices
  without a timeline enqueue a fresh signal through the same pending owner.
  Failure leaves access and caller output unchanged with the original diagnostic.
- Begin with `initialized=false` orders discard behind any accepted predecessor;
  only the next recorded use initializes the required aspects. An uncommitted
  recording remains BUSY. Shared textures have private attachment usage for clears.
- Fence export returns the original borrowed native event identity. Output-state
  free-members releases each fence and both owned arrays. Imported memory and
  fences retain their device independently of public device/queue references.

## Deliberately retained limits

This is **not** all Dawn shared-image policy. Like the retained implementation,
Begin ignores `concurrentRead` and its extension chain: there is one exclusive
access state, not concurrent readers.
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
silently substituted. Multiplanar features remain absent; advertising IOSurface
import does not enable NV12 or unsupported pixel formats.

Ordinary external-texture planes also permit genuine Metal CopySrc blits, using
the same native plane identity. This does not imply YUV or color conversion.

## Focused evidence

- `make -C runtime/c/native check-shared-memory`: public ABI over real resource,
  device and pending owners with private Metal call ledgers. Covers exact
  provenance, sRGB channel order, no import zero-overwrite, queued producer dependencies, early public fence/texture release,
  zero-work timeline forwarding, fresh asynchronous signals, first-error detail,
  unknown-work retention and
  final cleanup. No GPU results are synthesized.
- `make -C runtime/c/native check-iosurface-metadata` on Darwin: actual BGRA/RGBA
  IOSurface import, properties before texture creation, exact-format rejection,
  compatible sRGB views and early native/public releases. This selects physical
  Metal but submits no GPU work.
- `make -C runtime/c/native check-shared-features`: host-only independent queried
  capability admission, duplicate request sets, enumeration, disabled device
  features and unknown/private-slot alias rejection.
- `make -C runtime/c/native check-iosurface-shared` on Darwin: requests both
  advertised features, imports actual producer-written BGRA pixels, begins
  access, copies and reads back every pixel through the GPU, then ends access
  with a fresh exported Metal shared event. Queue completion precedes cleanup;
  native and public surface/texture leases are released early.
- `make -C runtime/c/backends/metal check`: existing ownership/native-call tests;
  external typed-call controls include CF retain/release, single-plane admission,
  shared format/usage arguments and encoded GPU waits without producer polling.
- `make -C runtime/c/backends/metal abi-compile`: host typed-call compilation for
  the arm64/x86_64 branches, not Apple SDK verification.
- `make -C runtime/c/backends/metal sdk-abi`: actual Apple SDK comparison, including
  CFTypeRef/event calls; requires an installed Apple SDK.

Both actual IOSurface consumers pass on Apple M3. This establishes their native
metadata, pixel and new-event path. External producer-fence waits and Fawn browser
external-image acceptance remain separate gates; native tests do not qualify them.

## Linux Vulkan OPAQUE_FD memory and SYNC_FD fences

On Linux Vulkan the adapter advertises Dawn's `SharedTextureMemoryOpaqueFD`
(`0x50020`) and `SharedFenceSyncFD` (`0x50027`) together, only when both
`VK_KHR_external_memory_fd` and `VK_KHR_external_semaphore_fd` exist, SYNC_FD
semaphores are importable and exportable, and an optimal BGRA8 attachment image
is importable from OPAQUE_FD memory. A device requesting either feature selects
the extensions (`doe_vk_options.enable_external_fd`); creation fails if they are
not really enabled. Other devices enable nothing new.

- Import reads the producer's exact `VkImageCreateInfo` (2D, one mip, layer and
  sample, optimal, exclusive, only `VkExternalMemoryImageCreateInfo` chained) and
  recreates the image with those flags and usage, then imports a duplicate of the
  memory descriptor (dedicated when the producer's was). One backend image serves
  every texture created from the memory; textures admit only the producer's
  format, size, a subset of its usage and, for mutable images, the srgb pair.
- Begin records, without submitting, an acquire from `VK_QUEUE_FAMILY_EXTERNAL`
  repeating the producer's release layouts into the device's access batch. Each
  fence's sync file is imported into a fresh binary semaphore that submission
  waits for. An UNDEFINED layout or `initialized=false` discards contents, which
  the canonical attachment owner zero-initializes. An imported image is unusable
  outside Begin/End. One acquire occupies the batch: a later Begin first submits
  an earlier one, so a rejected acquire never discards another access's.
- The next queue submission records its work after that acquire, so acquire and
  work share one `vkQueueSubmit`. Every submission releases, at its end, each
  imported image it used, back to `VK_QUEUE_FAMILY_EXTERNAL` without a layout
  change, all sharing one exportable SYNC_FD signal exported once on acceptance.
  Within the same access, the next recording that uses the image transfers it
  back from `VK_QUEUE_FAMILY_EXTERNAL` before any command touches it (same queue,
  later in submission order: no producer wait, same layout and contents).
- End takes the last accepted release of the access as its fence, without a
  submission, when the last submission that used the image released it. An
  access no submission used adds its release to the batch, so acquire and release
  share one submission; an image acquired by a submission that did not use it is
  released by a new one. The end state reports the released layout and whether
  contents are initialized; its one fence and value live inside that fence, which
  FreeMembers recognizes and releases. End requires an output state. Destroy
  leaves the shared image (BUSY); the last memory release frees it. Every
  external semaphore lives until its recording is proven terminal; unknown
  completion keeps it. A recording that never reached the queue undoes the
  acquire, reacquire and release it recorded, so failure leaves the image's
  ownership as it was; an access ending through texture destruction forgets any
  queue release.

Evidence: forced-Doe Chromium canvas presentation on RADV and ANV with the exact
pixel oracle (browser/chromium). The backend's two-device test
(`backends/vulkan` `external-import`) covers acquire, release, a folded
two-submission access with reacquire, and undo of an unsubmitted acquire; there
is no native-level two-device test.
