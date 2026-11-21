# Gamma workspace extraction boundary

**Initial slice:** this is an ownership and dependency gate, not a source move.
The transitional `browser/workspace/` package still runs from Fawn. Move it only
after Gamma builds and passes the same tests independently.

| Owner | Source boundary |
| --- | --- |
| Fawn | `browser/chromium/`, Chromium overlays, launcher, runtime selection, and native capture implementation. Optional clients bind a browser capability; no Gamma service is started or required by the browser. |
| Doe | Native GPU runtime, shader compiler, public WebGPU packages and bridge. No workspace dependency. |
| Gamma | Move `browser/workspace/src/` as a unit: authority checks, durable journal, dispatch, browser adapters (including the optional Fawn client adapter), graph/recipe engines, UI, and synchronization. Own new native desktop adapters here too. |

## Receiving unit and references

- Bring the workspace's package and lockfile, test runner, tests and fixtures into
  Gamma with production code, separate from research imports. Preserve the current
  package/command identity for the first parity run; retire it after migration.
- Relocate the live service/qualification config and schemas and their contract
  paths from Fawn's root `config/` only after inventorying all readers. Inspect
  `schema-targets.json`, `tool-surfaces.json`, and product strategy config before
  removing entries. Keep historical files and schema/evidence identities where
  frozen receipts still refer to them.
- Move Gamma-owned workflow qualification and deploy manifests; leave Fawn/Doe
  execution and integrated comparison harnesses on the Fawn side. Their optional
  workspace runner should take a pinned artifact as an explicit input. Do not
  relabel old reports, strategy keys or frozen receipts.
- Keep stock Chromium/Chrome selectable without the Fawn fork. Optional native
  capture is a browser capability, not a holder of workspace permissions,
  dispatch, journal or private state.

## Cutover gates

1. Stage the proposed code and run
   `python3 browser/chromium/scripts/check-workspace-independence.py`. The guard
   scans the Git index for direct workspace references in the Chromium, runtime,
   and packages trees; it also works from a sparse checkout. It is not a
   transitive dependency audit or browser smoke.
2. Run Fawn startup, navigation and WebGPU checks, and Doe's standalone smoke
   in clean jobs with no Gamma or legacy workspace installation. Run the pinned
   end-to-end GPU workload with the explicit optional workspace runner separately.
   The historical diagnostic below passed without workspace execution;
   current linked capture is a separate gate.
3. In Gamma, run workspace unit and stock Chrome browser tests from the built
   production package without research sources or a Fawn checkout. Compare
   recovery, permission, and replay behavior against the donor.
4. Only then switch package ownership and remove redundant active donor files.
   Do not delete historical evidence.

## Historical Fawn standalone and forced Doe execution

**2026-11-21 UTC | Darwin arm64, Apple Metal 3 | Diagnostic, not a release gate**

An existing Chromium `158.0.8108.0` Fawn app launched headlessly with sandbox
requested, a disposable home and profile, and a private pipeline-cache location.
The browser harness used Playwright from the Chromium integration; it neither
launched nor imported a Gamma or legacy workspace service. The fixture browsed
only loopback.

| Input | SHA-256 |
| --- | --- |
| Fawn wrapper | `77d3ce31f90082e987d886c4ae5bca17299b463995cef4336447e660e816f8e2` |
| Chromium-real launcher | `728f6f7d8e9eb42d483bbefc9e69051e2264908b16a755af4fbdc7e69a49e65f` |
| Chromium Framework | `2447151a5a132f6bc8bc5892a9a9a63a94c2ffc874a355764669faa40e315d59` |
| Explicit Doe dylib | `06fc97c417e935811d7f12719f8ffe0381937877011ec83caa61932eb6feedaf` |
| Retained local smoke JSON | `414a62f2e64cbcb251c004b592a5944494ee52b67d406fd18fc1d27c06ae3dc7` |

The explicit library matched its Doe package manifest at source
`f25951af26cfefeea643f097f81a86abb235d37a`. The separately bundled default
library had different bytes and was not used. The test runner source was
`9234bc55953c43af7d53c7335d8fe24282e59dc6`; this identifies the test code, not
a rebuild of the historical browser from that revision.

Results:

- The native WebGPU smoke in forced Doe mode passed compute, render, render
  bundles, indirect draw, timestamps, canvas and external-texture operations.
  Its increment readback was `[2, 3, 4, 5]`; the selector recorded no fallback.
  The smoke's embedded report hash was
  `0cde3bd8ac3c3bd8273caa3bb6a259b0d649b38c49462b35548af9630c098b4b`.
- A separate fresh-profile fixture loaded a loopback document, dispatched a
  one-workgroup WGSL write and copied the result to a mapped buffer. It read
  `42` on an `apple` / `metal-3` adapter with `isFallbackAdapter: false`.
  An independent process listing located the GPU child of this exact fixture;
  `lsof` observed the expected manifest-matching dylib mapped in that process.
- Pointing the same wrapper at a missing Doe library exited with status 1 and
  `no fallback` before producing a page. This is a launcher-boundary refusal,
  not a test of raw Chromium's runtime error reporting.

This supports browser independence and physical Doe execution for these exact
historical bytes. It does not qualify a newer linked Chromium/capture image,
test native capture with Gamma, or prove all platforms, sandbox enforcement,
performance, or deployment readiness. No donor removal follows from this
diagnostic alone.
