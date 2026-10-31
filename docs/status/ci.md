# CI status

Ordinary push and pull-request CI runs one Linux C compiler shared-library
build and native-consumer smoke, alongside the separate incremental publication
hygiene guard. [The CI contract](../process.md#continuous-integration) owns exact
commands, checked assertions, and exclusions. Zig, Lean, JavaScript package,
Windows package and upstream quirk-mining checks are explicitly manual, as are
hardware, browser and release qualification workflows.

The full manual publication scan still reports pre-existing private workstation
paths. Automatic lint rejects newly introduced paths rather than replaying that
historical debt. Its
[scanner](../../scripts/check-publication-hygiene.py) remains authoritative.

The AMD Vulkan smoke and release, Fawn matrix, and Windows D3D12 qualification
workflows require an exact full Git revision and an approved self-hosted runner
name. AMD Vulkan workflows run the migrated Zig build and gate sequence before
strict physical comparison. The Fawn matrix retains each workload attempt,
emits DoeLab learning records from raw evidence, keeps hardware-host suites
unsigned, and uploads evidence even when a lane fails. Windows D3D12 has a
separate manual workflow around the governed local D3D12 runner. These
qualification workflows seal collected files with SHA-256 and upload failures
as well as successful results; protected signing remains a separate
environment-owned step.

Workflow files under `.github/workflows/` are the source of truth.

The manual self-hosted Linux native-freshness job rebuilds and stages the
platform package, then clean-installs the wrapper and staged platform tarballs
under Node, a pinned Bun runtime, and a pinned Electron main-process runtime.
Each lane must execute its shipped first-kernel oracle without resolving a
workspace library before the workflow performs the stale-artifact rejection
check. The same job also requires the bounded repeated-process,
concurrent-process, and same-process lifecycle diagnostics for all three
runtimes. Electron uses an explicitly installed executable, creates no
renderer, and earns no browser evidence.

The manual package workflow runs the doe-gpu package tests and inspects the packed
contents. It replays no GPU work and grants no new evidence; hosted execution
skips native checks when staged platform artifacts or physical GPUs are absent.
The native integration suite, the tool-surface gate
(`bench/gates/tool_surface_gate.py`), and the HoloScript Electron diagnostic
contract run on demand or in the manual self-hosted workflow. Repository-wide
performance remains advisory rather than a promoted JavaScript-runtime release
requirement.

The separate manual native-candidate workflow owns the macOS arm64 package
admission path. It requires an exact full Git revision and approved Apple
Silicon runner, pins Node, Bun, and Electron, stages the Darwin platform
package, and emits schema-version-2 candidate and reliability receipts. The
tested wrapper and platform tarballs stay beside those receipts in a
self-contained bundle. An independent gate verifies source-commit blobs,
tarball members and hashes, completeness against the runtime set the candidate
declares with `--runtimes` (Node mandatory; this workflow declares Node, Bun
and Electron, and a Node-only candidate makes a Node-only claim), tuple identity, oracle,
replay, lifecycle, and reliability joins before the workflow seals and uploads
all passing or failed evidence. The workflow does not publish to npm or sign a
qualification decision.
