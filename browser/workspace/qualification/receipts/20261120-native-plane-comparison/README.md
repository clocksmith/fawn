# Exact restricted I420 frame comparison

Fawn can test whether the conversion inputs of two restricted captures are identical before repeating Blitzen's full RGBA copy. The native producer keeps one bounded copy of the three planes after Chromium confirms an element restriction and observes its capture version. It compares in the originating isolated world. The next accepted canonical digest can be reused only within that same reader when the native serial, dimensions and tile size match. Chromium keeps its ordinary capture buffers and releases them normally.

Eligibility requires full, unrotated, tightly packed, even-sized I420 in CPU-readable read-only shared memory, valid SDR color data, no HDR metadata, and a matching capture source epoch and restriction. Updating the restriction or closing its queue retires the snapshot. Cloned, transferred and page-created wrappers have no lease. Ineligible samples go through the existing RGBA conversion and digest. The feature defaults off and enables independently of tracing through `--enable-blink-features=FawnRestrictedPlaneEvidence`.

The stock Chromium software capture reproduces the prior 32-tile collision. Two distinct 512 × 256 frames have Y/U/V values 40/115/176 and 40/114/176. Their native SHA-256 digests match the previous capture byte for byte. Every canonical pixel is respectively RGBA 114/5/2/255 and 114/5/0/255. The external sampler gives the same digest to both. The C++ executable reconstructs the captured bytes in Chromium read-only shared-memory VideoFrames, uses the pinned Chromium CPU Skia paint path to check every pixel, and checks the serial across equal independent buffers, chroma, color space, crop, world, capture epoch, unsupported storage and retirement.

Four Blink native translation units and the generated V8 VideoFrame and runtime feature translation units compiled with the pinned toolchain. All nine optional patches applied forward and in reverse to copies of the sealed source. The independent stock browser workflow refused stale evidence, reconciled one fresh effect and copied 291,200 host RGBA bytes during its measured interval. It made zero external model calls. The native path still needs a linked, sandboxed Fawn run on a real restricted capture. Its availability and latency on a physical backend remain open. An equal 512 × 256 native comparison logically reads 196,608 bytes from each of two host buffers. The largest allowed retained snapshot is 6 MiB per restriction. These byte counts describe the native operation, not a measured end-to-end saving.

To repeat the C++ test on the pinned Linux Chromium component build, apply the existing frame-import stack including the two fixes in `38224c1`, then `browser/chromium/patches/resident-restricted-i420.patch`. The test builds in a temporary directory and uses the existing Linux component libraries.

```sh
python3 browser/workspace/qualification/native/check-restricted-i420.py \
  --chromium-src "$CHROMIUM_SRC" --build "$CHROMIUM_BUILD"
```

For a linked acceptance, set `BLITZEN_TEST_BROWSER_CONFIG` to the pinned Fawn browser profile. Use a disposable visible sandboxed browser, explicitly grant each restricted capture, and run both files from `browser/workspace`. The color test requires independent repeated buffers to share a nonzero serial, the collision to change that serial, and main-world and cloned frames to be ineligible. The ordinary workflow requires a real unchanged dispatch recheck to report zero RGBA copy/hash bytes, a changed stale recheck to read canonical pixels, and authoritative reconciliation.

```sh
export BLITZEN_TEST_ELEMENT_CAPTURE=1 BLITZEN_TEST_FAWN_PLANE_EVIDENCE=1
export BLITZEN_TEST_COMPOSITOR_DIGEST=webgpu BLITZEN_REVIEW_COMPOSITOR_TRANSPORT=external-texture
node test/browser/restricted-frame-color.test.js
node test/browser/canvas-review.test.js
```
