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

The retained C build manifests bind the loaded runtime/compiler component hashes.
Browser source provenance is incomplete. The receipt helper detected executable
metadata changes during launch and conservatively left those executable hashes
unknown. That limitation is preserved. GPU-driver identity is also unknown.

The follow-up profile still found a submission wait after the buffer-use change.
Further tracing identifies a separate opaque-canvas alpha-clear Load/Store draw
on the same external texture after the frame producer. Its initialization and
texture ownership need queue ordering; removing that alpha clear would change
the opacity contract. These observations guide the next fix and do not prove a
completed browser speedup.

Each cohort has its own receipt archive, so normalization never mixes headed,
headless, interrupted, baseline or candidate launches. Private filesystem
prefixes are redacted in publication copies. Numeric samples, execution clocks,
artifact hashes, flags, errors and limitations are unchanged;
[`retention.json`](retention.json) records original byte hashes and the analyzer
identity. Reanalysis time is `reportedAt`; execution dates come only from the
original launch receipts.
