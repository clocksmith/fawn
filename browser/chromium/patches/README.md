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

The patches retain upstream copyright and license notices. A rebase requires
updating the pins, reviewing every source change, rebuilding, and repeating
provider attribution and application correctness checks; changing a revision
constant alone cannot establish compatibility.
