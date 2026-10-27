# Pinned Chromium integration patches

The source revisions in
[`apply-runtime-patches.sh`](../scripts/apply-runtime-patches.sh) pin the Chromium
checkout and its Dawn dependency. The script checks both revisions and both
patches before applying changes, accepts an already-applied patch set, and rejects
partial or conflicting applications. Source checkout and toolchain downloads
remain Chromium's `fetch`/`gclient` workflow.

```bash
browser/chromium/scripts/apply-runtime-patches.sh --check
browser/chromium/scripts/build-release-external.sh --release-profile local
```

`chromium-doe-runtime.patch` carries explicit provider switches to the GPU
process, selects the external WebGPU backend for page requests, transfers canvas
textures through Chromium's Skia readback/upload path, and preloads the selected
library before Linux's existing GPU sandbox starts. Forced Doe rejects unavailable
providers, the disable switch, and fallback adapters. Shared-buffer mailbox
imports remain explicitly unsupported.

`dawn-doe-runtime.patch` loads the selected provider's required native functions
into the existing WebGPU-on-WebGPU backend and pumps its instance callbacks.
Dawn's wire and validation wrappers remain in this recovered integration. This
is a buildable diagnostic route, not acceptance of the contract's direct Doe wire
ownership or a browser performance claim. The current build and execution state
belongs in [runtime status](../../../docs/status/runtime-backends-and-bench.md).

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
device. This neither advertises a WebGPU feature nor accepts Dawn-private chains
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

The patches retain upstream copyright and license notices. A rebase requires
updating the pins, reviewing every source change, rebuilding, and repeating
provider attribution and application correctness checks; changing a revision
constant alone cannot establish compatibility.
