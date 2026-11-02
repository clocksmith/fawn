# Source-bound macOS browser iterations

Disposition: **diagnostic; the all-tests win target is not met**. These records
feed the existing [benchmark inventory](../../CURRENT.md), through
[`sources.json`](../../sources.json). They preserve adverse results and the
interrupted run. They do not enter the publication claim index or supersede
historical observations.

| Cohort | Mode | Runtime payload | Generated result |
|---|---|---|---|
| Compute, six repetitions per arm | Headed | `a3af9e521` | [summary](headed-compute-summary.md) |
| Particles, interrupted after nine of eighteen launches | Headed | `a3af9e521` | [summary](headed-particles-interrupted-summary.md) |
| Particles, six repetitions per arm | Headless | `a3af9e521` | [baseline](headless-buffer-baseline-summary.md) |
| Particles, six repetitions per arm | Headless | `8c4bc670e` | [buffer-use candidate](headless-buffer-candidate-summary.md) |
| Particles, six repetitions per arm | Headless | `7c6ad42c2` | [texture-use candidate](headless-alpha-candidate-summary.md) |
| Particles, six repetitions per arm | Headless | `aa402f004` | [completion-wake candidate](headless-wake-candidate-summary.md) |

The three arms use stock Chrome for Testing 158, Fawn 158 with Dawn, and the
same Fawn browser with an explicitly selected C payload. The stock build is
official PGO; Fawn is a local non-PGO build. The direct Doe/Dawn comparison
separates the provider effect from that browser-build difference.

The buffer-use candidate still loses completion and total-CPU metrics against
stock; its direct Doe/Dawn comparison is inconclusive. See the generated tables
for separate metric outcomes and uncertainty. The desktop remained active, and
six launches cannot qualify release tails. Headless completion is a different
scope from visible presentation. Particle visual checks do not provide a full
independent particle-state oracle; the compute receipts retain their initial and
final full-buffer SHA-256 oracle results.

The texture-use candidate also misses the target: its median completion ratio
is 1.278 against both stock and the same-Fawn Dawn control. Against Dawn, the
95% interval is [1.243, 1.306]; the median launch-p95 ratio is 1.292
[1.125, 1.622]. These are losses under the existing classifier. The stock tail
comparison is inconclusive. This separate cohort retains its own controls;
differences across cohorts are not an isolated estimate of the patch's effect.

The completion-wake candidate also loses median completion against stock and
the same-Fawn Dawn control. Its separate metric outcomes and uncertainty are in
the linked generated summary. This run predates the ready-callback fast path in
`50cf8feb8`; it provides no measurement of that later change.

The retained C build manifests bind the loaded runtime/compiler component hashes.
Browser source provenance is incomplete. The receipt helper detected executable
metadata changes during launch and conservatively left those executable hashes
unknown. That limitation is preserved. GPU-driver identity is also unknown.

The follow-up profile still found a submission wait after the buffer-use change.
Further tracing identifies a separate opaque-canvas alpha-clear Load/Store draw
on the same external texture after the frame producer. Its initialization and
texture ownership needed queue ordering; removing that alpha clear would change
the opacity contract. Commit `05c87ab08` orders accepted, tracked texture uses
on the GPU queue and preserves initialization epochs and the latest release
point. Commit `7c6ad42c2` fixes the shared-image descriptor's documented default
2D dimension on Metal, Vulkan and D3D12. The extended
[IOSurface consumer](../../../../runtime/c/native/tests/iosurface_shared.c)
passed on the Apple M3: separate producer/alpha submissions preserved RGB,
changed alpha from zero to 255, and exported the latest point to an independent
consumer queue. Those correctness checks do not qualify the timed page or
establish a browser speedup. The retained comparison above still loses.

The separately instrumented [trace](alpha-clear-diagnostic.log.gz),
[launch receipt](alpha-clear-diagnostic-receipt.json.gz) and
[diagnostic build binding](alpha-clear-diagnostic-build.json.gz) support that
resource attribution. Their timings are excluded from the comparisons. The
pinned Chromium sources show the
[opaque-canvas hook](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/third_party/blink/renderer/modules/webgpu/gpu_canvas_context.cc#620),
[mailbox release](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/third_party/blink/renderer/platform/graphics/gpu/webgpu_mailbox_texture.cc#307)
and [alpha-only draw](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/third_party/blink/renderer/platform/graphics/gpu/webgpu_texture_alpha_clearer.cc#60).

Each cohort has its own receipt archive, so normalization never mixes headed,
headless, interrupted, baseline or candidate launches. Private filesystem
prefixes are redacted in publication copies. Numeric samples, execution clocks,
artifact hashes, flags, errors and limitations are unchanged;
[`retention.json`](retention.json) records original byte hashes and the analyzer
identity. Reanalysis time is `reportedAt`; execution dates come only from the
original launch receipts.
