# Pinned Chromium integration patches

The source revisions in
[`apply-runtime-patches.sh`](../scripts/apply-runtime-patches.sh) pin the Chromium
checkout and its Dawn dependency. The script checks the revisions and all applicable
patches before applying changes, accepts an already-applied patch set, and rejects
partial or conflicting applications. Source checkout and toolchain downloads
remain Chromium's `fetch`/`gclient` workflow.

```bash
browser/chromium/scripts/apply-runtime-patches.sh --check
browser/chromium/scripts/build-release-external.sh --release-profile local
```

`chromium-doe-runtime.patch` carries explicit provider switches to the GPU
process, selects the external WebGPU backend for page requests, and preloads the
selected library before Linux's existing GPU sandbox starts. On Windows the GPU
process maps the provider and the DXC, DXIL and FXC libraries beside it before
`LowerToken()`, since the lowered token cannot map new images; the provider's
later full-path loads reuse them. Canvas textures are
imported zero-copy when the device enabled Doe's interop (Linux: Vulkan OPAQUE_FD
memory with SYNC_FD and OPAQUE_FD semaphores; macOS: IOSurface with
MTLSharedEvent fences, packed 8-bit surfaces without storage usage) and the
backing exports its memory; otherwise they transfer through Chromium's Skia
readback/upload path. The route is chosen before any import and never falls back
after an admitted import fails. Forced Doe rejects unavailable
providers, the disable switch, and fallback adapters. Shared-buffer mailbox
imports remain explicitly unsupported.

`dawn-doe-runtime.patch` loads the selected provider's required native functions
into the existing WebGPU-on-WebGPU backend and pumps its instance callbacks. The
backend releases each inner render and compute pass encoder after ending it (a
pass retains its encoder and recorded attachments). An IOSurface access ends with
the provider's own release fence and value, waits on a provider
commands-scheduled future when one is returned, and reports no outer future, so
no shared image retains the device. `GetMTLDevice` reports nil for a device of
another backend.
Dawn's wire and validation wrappers remain in this recovered integration. This
is a buildable diagnostic route, not acceptance of the contract's direct Doe wire
ownership or a browser performance claim. The current build and execution state
belongs in [runtime status](../../../docs/status/runtime-backends-and-bench.md).

Zero-size buffer mappings belong to Dawn's outer buffer. Dawn skips the backend
unmap for these buffers, so the bridge creates their inner buffers unmapped;
otherwise an application-level unmap leaves the inner buffer mapped and a legal
render submission fails. Nonempty mappings and the public C buffer contract are
unchanged. After rebuilding the patched browser, run the boundary regression
against each provider:

```bash
node browser/chromium/tests/zero-size-buffer.mjs \
  --browser /absolute/fawn/chrome --provider dawn --out /new/output/dawn
node browser/chromium/tests/zero-size-buffer.mjs \
  --browser /absolute/fawn/chrome --provider doe \
  --doe-library /absolute/libwebgpu_doe_c.dylib --out /new/output/doe
```

The regression checks zero/nonzero mapping state, legal submission and rendered
pixels, plus rejection of buffers left mapped. It is a diagnostic, not provider
ownership or performance qualification. A source patch or an emulated descriptor
handoff does not establish that an existing browser binary contains the repair.

The external connection projects Dawn's private instance, adapter and device
tuning descriptors at one typed boundary. Dawn retains its outer validation and
policy; every public or unknown feature requirement, limit, callback and adapter
identity requirement remains intact. `DawnInternalUsages` is consumed by the outer
texture conversion, which emits ordinary WebGPU usage bits. GL loader callbacks
remain required when an external GL backend is actually selected. Native Dawn
forwarding retains its existing behavior.

The outer adapter applies `DawnConsumeAdapterDescriptor` only after successful
device creation. Its original flag stays unchanged for that owner; the private
node is absent from the external device request. Remaining unknown chains still
reach the external provider for admission or rejection.

Browser C devices must implement the private typed operation
`WGPUStatus doeNativeDeviceDisablePersistentCache(WGPUDevice)`. The bridge requires
the symbol and applies it immediately after creating an inner device, before
publication or shader creation. A refusal destroys and releases the unpublished
device. C browser disk shader persistence is disabled; the per-device memory cache
remains available. Dawn's isolation-key cache descriptor stays with the outer
device. After that policy succeeds, the bridge attaches the outer
device's blob cache through the provider's optional
`doeNativeDeviceConfigurePipelineBlobStore`. Compute pipelines then load and
store enveloped Vulkan driver caches through Chromium's WebGPU blob cache, which
the browser process persists; the sandboxed provider opens no cache file. A
provider without the symbol, or a refused store, logs
`runtime_pipeline_blob_cache_unavailable` and creates pipelines uncached. Each
configured device logs `runtime_pipeline_blob_cache` counters after the inner
device is destroyed. Lookups run synchronously during pipeline creation; a miss's
store may arrive later from the provider's blob worker thread, until the inner
destruction returns, and Dawn's blob cache and Chromium's caching interface
accept stores from any thread. This neither advertises a WebGPU feature nor accepts Dawn-private chains
in the C constructors. An older runtime without the operation rejects at load.

The focused CPU boundary control uses the actual generated header and provider:

```bash
python3 browser/chromium/tests/test_external_runtime_projection.py \
  --dawn-source CHROMIUM/src/third_party/dawn \
  --webgpu-header CHROMIUM/src/out/fawn_release/gen/third_party/dawn/include/dawn/webgpu.h \
  --doe-library PRIVATE_C_LIBRARY --expected-private-policy present \
  --out bench/out/external-runtime-projection-control
```

It checks external/native projection, retained core requirements and rejected
unknown chains, and policy failure cleanup ordering. A CPU pass establishes no
browser, physical-adapter, presentation or performance result.

External provider errors retain their Validation, OutOfMemory or Internal type
through Dawn's error scopes and uncaptured-error route. The observed layout,
bind-group and compute-pipeline constructors reject null inner handles before
publishing a usable outer object. An unreported null is a backend contract error.
Native Dawn forwarding retains its existing error handling.

External callback userdata is an opaque, non-reused registry token. Lookup takes
a strong device reference before releasing the registry mutex; retirement releases
references outside that mutex. The existing recursive device mutex is present
before provider construction and keeps its identity through initialization.
A loss before publication rejects creation without touching an uninitialized
queue. Failed creation and normal destruction retire their callback entries.

An OPAQUE_FD access passes the provider its inner fence handles from inline
storage and the frontend's contiguous signaled values directly; the provider owns
how acquire and release reach its queue.

The separate Linux CPU controls use the native Chromium compiler flags, actual
Dawn error scopes and reference counts, and Chromium's pointer-safety libraries:

```bash
python3 browser/chromium/tests/test_external_runtime_errors.py \
  --chromium-source CHROMIUM/src --build-dir out/fawn_release \
  --out bench/out/external-runtime-errors-control
```

These host controls exercise typed errors, null rejection, callback reference
races, reentrant retirement, late callbacks and initialization publication under
the real device mutex. They do not qualify a browser or GPU execution.

The selected Skia mailbox fallback transfers pixels through CPU staging memory.
Its byte layout follows the declared SharedImage/Dawn format, even if the backing
SkImage or surface uses another channel order. The shared conversion preserves
image dimensions, alpha type and color space. The existing RGBA8, BGRA8 and
RGBA-F16 allowlist applies on Apple as well as the other platforms.

The real Skia raster CPU control exercises both transfer directions, opposite
backing/declaration channel orders, F16/HDR, alpha, color-space and row-stride
cases. It also executes the old interpretation and verifies its channel mismatch:

```bash
python3 browser/chromium/tests/test_skia_fallback_pixels.py \
  --chromium-source CHROMIUM/src --build-dir out/fawn_release \
  --mac-build-dir out/fawn_macos_arm64 --out bench/out/skia-fallback-pixels
```

The optional Mac target is a compile check; raster execution is on the Linux
host. Browser pixels and physical GPU behavior require their separate gates.

The patches retain upstream copyright and license notices. A rebase requires
updating the pins, reviewing every source change, rebuilding, and repeating
provider attribution and application correctness checks; changing a revision
constant alone cannot establish compatibility.

The macOS app wrapper and diagnostic lane prepare immutable app-local runtime
closures through `scripts/stage-app-runtime.mjs`. The C runtime and its compiler
sibling publish together under a content identity. Refreshing never overwrites a
library that a GPU process may still have mapped. Existing closures must match
their bytes before reuse; failed preparation preserves the earlier bundle. This
also keeps selected runtime files within the GPU sandbox's readable app bundle.

On Linux, `--gpu-sandbox-start-early` seals the GPU process (`Seccomp: 2`) before
Mesa spawns GL worker threads. `content/common/gpu_pre_sandbox_hook_linux.cc`
extends `UsesEarlyExternalGlxResources()` across `x11`, `wayland`, and `headless`
ANGLE-GL routes when forced Doe is active, preloads `libEGL.so.1`,
`libEGL_mesa.so.0`, `libGLESv2.so.2`, and `libgbm.so.1` alongside the GLX/Vulkan
closures prior to sealing, and grants broker read-only access only to regular,
root-owned, non-writable GLVND EGL vendor JSON manifests
(`/usr/share/glvnd/egl_vendor.d`, `/etc/glvnd/egl_vendor.d`) while driver library
directories remain denied. Native Dawn and non-ANGLE-GL routes stay unchanged.


## Reviewed coordinate input on Linux

The Linux patch set adds three experimental CDP Input commands. Capture takes an
integer rectangle in root viewport CSS pixels and the point to click inside it.
It returns the exact unpremultiplied sRGB RGBA8 bytes, their SHA-256 digest,
document token, surface ID, device scale and Viz frame index. The bytes are
Viz's own reference copy of the rectangle from the frame it drew next. Before Viz
is armed, the renderer binds the node it hits at the point. The returned
evidence ID belongs to the creating CDP session and pays for one click at that
point. A second capture in the same session supersedes the previous review.
Reviews in different sessions of one page stay open together: Viz keeps up to
16 reviews per surface, each with its own gate, rectangle and reference, and
the page keeps one bound node per review. `Input.getVisualEvidenceStatus`
reports whether a review still holds without spending it, with the first
failing condition and the latest frame Viz verified. The initial scope
requires a visible top-level page, one surface, unit device scale and zoom, and
a region at most 1024 pixels per side. Concurrent-write textures, overlay
candidates, child frames, transition content and video are unsupported.

While a review is open, Viz copies the reviewed rectangle out of every frame it
draws for the reviewed surface and compares the bytes with the reference. The
copy is attached when the aggregator collects the surface's copy requests for
that draw, so each verdict belongs to the frame drawn; a request still pending
when a newer frame activates is dropped, never carried over. Viz publishes
verdicts in frame order through a shared gate. A frame whose reviewed bytes
differ, or whose copy fails, closes the review with a lock-free
compare-and-exchange that orders it against browser admission; nothing is
verified after it. Frames that leave the rectangle unchanged keep the review
open. No renderer-reported damage or bounds take part.

Viz ends the review outright for a frame it cannot verify by copying: one that
references child surfaces, may contain video, carries transition or shared
element content, concurrent-write or overlay-candidate resources, or a device
scale or page scale other than 1. It also ends it on submission to another
surface of the sink, eviction, sink teardown or display context loss. After
final document, surface, scale, point and native widget hit testing, the
browser consumes the open review, checks that the original Viz owner's pipe is
alive, and admits the click. It records the frame active at admission and
releases the click only after Viz has verified a drawn frame at or after it;
if Viz refutes first, loses the review or gives no verdict within one second,
the admitted click is cancelled.

Admission does not authorize retargeting. The browser re-checks the bound widget
and document and sends the click to the renderer that bound the target. In one
main-thread task the renderer re-checks the document token and that the bound
node is still hit at the point, then dispatches the press and release, or
cancels. A pixel-identical overlay, a raised element or a navigation after
review therefore cancels delivery even when no new frame reached Viz. Results:

| `admitted` | `delivered` | `reason` | Meaning |
|---|---|---|---|
| false | false | `stale-compositor-revision`, `stale-document`, `target-changed`, `outside-reviewed-target`, `unknown-review` | Refused before admission |
| true | false | `stale-compositor-revision`, `recipient-changed`, `document-changed`, `target-changed`, `recipient-gone`, `recipient-unsupported` | Admitted, cancelled before the page saw it |
| true | true | `delivered` | Delivered to the bound document and node |

A repaint after admission does not make the admission stale: the click is
delivered to its bound node. A GPU fence or frame order proves which pixels
were shown, not who receives the event; recipient checks stay in the renderer. The browser guarantee covers which pixels were
reviewed and which document and node receive the click. An application still
enforces authorization, record version and idempotency when it writes.

Animation outside the reviewed rectangle no longer refuses clicks. The cost is
one readback of the rectangle per drawn frame of the reviewed surface while a
review is open, and the reviewed surface's root pass drawn separately for it.

A refused capture names who refused and why, for example
`visual surface changed or unsupported (viz:frame-pending)` or
`(browser:surface-changed)`. Refusals that only mean the surface was mid-update
(Viz `frame-pending`, `not-current-surface`, `other-surface-submitted`, or the
view moving to a new surface during capture) are retried twice, a frame apart,
on the view's current surface with a fresh gate; nothing has been shown to a
reviewer before the reference returns. Any other refusal is final, and no
earlier approval survives it: the caller reviews again.

On the qualification host (no hardware GPU), 300 clicks under unrelated
animation and main-thread load delivered 300 in every arm. Click round trip
p99 was 22 ms with software compositing and 55 ms with SwiftShader GL
compositing, where the verification readback completes a frame later;
admission itself is about 0.1 ms. The Doe arm's `--gpu-sandbox-start-early`
makes Viz fall back to software compositing, and no arm gets a WebGPU adapter,
so this does not compare Dawn with Doe. See
`reports/fawn-workspace/20261122-pixel-verified-review/decision.json`.

### Release qualification and the extension gate

`reports/fawn-workspace/20261122-release-qualification/release-receipt.json` records the non-component
build of this patch: every reviewed-click case on the cpu, dawn and doe arms, the load test, the workspace
and inbox repeats, and a comparison with an ordinary extension. The pass criteria for that comparison were
committed in `extension-gate-criteria.json` before any controlled-ordering result.

```sh
node browser/chromium/tests/reviewed-click-extension-control.mjs --arm fawn --browser "$FAWN_BROWSER" \
  --out "$FAWN_JSON" --attacks "" --forced-trials 10
xvfb-run -a node browser/chromium/tests/reviewed-click-extension-control.mjs --arm extension-fenced \
  --browser "$FAWN_BROWSER" --out "$EXTENSION_JSON" --attacks "" --forced-trials 10
```

`tests/extension-control/` is the control: the strongest reviewed click an ordinary MV3 extension can give,
in three modes. It runs in the same binary using only stock extension APIs. On the frozen requirements the
fenced mode matches the browser-owned path. The browser-owned path alone identifies the recipient inside a
closed shadow root, closes a review on a one-frame change, and needs no debugger permission.

### Qualification

`--fawn-reviewed-input-barriers=DIR` enables qualification pause points. They
stay inert unless the qualification creates `DIR/hold-admission` (pause after
validation, before the compare-and-exchange) or `DIR/hold-delivery` (pause after
admission and pixel verdict, before renderer delivery). The browser writes
`PHASE.ready`, reports `PHASE.viz-activation` once Viz has activated a
replacement frame on the reviewed surface after `PHASE.await-activation`
appears, reports `PHASE.viz-refutation` once Viz has drawn a frame whose
reviewed bytes differ after `PHASE.await-refutation` appears, and continues on
`PHASE.release`. Never set the switch in a product profile.

Run the shared arbitration test against the patched source, then the lifecycle
controls and the ordering qualification, each launching its own disposable
browser. The lifecycle test intentionally crashes only its own GPU process and
renderer.

```sh
c++ -std=c++20 -I "$CHROMIUM_SRC" browser/chromium/tests/reviewed-input-arbitration.cc -o "$TMPDIR/reviewed-input-control"
"$TMPDIR/reviewed-input-control"
node browser/chromium/tests/reviewed-input-lifecycle.mjs --browser "$FAWN_BROWSER" --out "$RESULT_JSON"
node browser/chromium/tests/reviewed-input-qualification.mjs --arm dawn \
  --browser "$FAWN_BROWSER" --patch browser/chromium/patches/chromium-reviewed-input.patch --out "$DAWN_JSON"
node browser/chromium/tests/reviewed-input-qualification.mjs --arm doe --doe-library "$DOE_LIBRARY" \
  --browser "$FAWN_BROWSER" --patch browser/chromium/patches/chromium-reviewed-input.patch --out "$DOE_JSON"
```

The qualification runs both orderings on one drawing, ordering 1 and the
pixel-identical target changes again under a running unrelated animation, an
animation that starts inside the reviewed rectangle after review (five trials,
each refuted by Viz before admission), an unchanged target, replay of a
consumed approval, invalidation on navigation, renderer restart and GPU loss
with fresh review afterwards, an application duplicate commit, and delivery
rates under unrelated compositor, main-thread and canvas animation. It
records barrier acknowledgments, the reviewed-input trace and event counts.
