# Chromium integration layer

`browser/chromium/` owns Doe's repository-local Chromium integration contracts,
scripts, milestone checks, and diagnostic artifacts. It is not the Chromium
checkout itself and it does not change the public `doe-gpu/browser` contract.
It lives in [Fawn](https://github.com/clocksmith/fawn); Doe is the compiler/runtime
component selected at the webpage's WebGPU boundary.

Fawn provides durable browser workspaces through the
[workspace service above stock Chromium](../../docs/fawn-workspace-contract.md).
This directory owns the separate, optional Chromium runtime-integration lane.
Chromium hooks and browser-runtime integration are added only when measured
benefits justify their compatibility and maintenance costs. Lane success means a released
Chromium-family archive runs an unchanged WebGPU application through forced
Doe with an independent oracle, physical hardware identity, reliable
lifecycle behavior, and a narrow compatibility or performance benefit. It
does not mean a general browser benchmark win or qualified workspace authority.

## Boundaries

- The external Chromium checkout/build workspace is selected separately by the
  lane tooling.
- C compiler/native runtime behavior belongs under `runtime/c/`; retained Zig
  behavior belongs under `runtime/zig/`.
- Browser schemas and shared policy belong under `config/`.
- Browser execution and release results belong in lane artifacts.
- Package evidence cannot promote this lane.

Doe changes only the WebGPU implementation seam. Chromium's process model,
sandbox, renderer, layout, media, accessibility, origin, and security policy
remain browser-owned.

## Source of truth

- Acceptance: [`plan.md`](acceptance.md)
- Milestones: [`bench/workflows/browser-milestones.json`](bench/workflows/browser-milestones.json)
- Contracts: [`contracts/`](contracts/)
- Live status:
  [`../../docs/status/runtime-backends-and-bench.md`](../../docs/status/runtime-backends-and-bench.md)

Do not add a parallel task list, strategy, or artifact inventory to this file.

## Directory map

| Path | Responsibility |
| --- | --- |
| `contracts/` | Formal runtime, benchmark, receipt, and release obligations |
| `scripts/` | Lane builders, checkers, and launch helpers |
| `patches/` | Pinned, replayable Chromium and Dawn source integration |
| `resources/` | Hosted background cycle in `fawn-cycle.html` and its `fawn-*.js` modules; standalone particle, fluid, and image workloads retained for benchmarks |
| `bench/` | Browser workload and milestone configuration |
| `artifacts/` | Checked-in diagnostics and governed browser evidence |
| `src/` | Optional external checkout link or workspace, not Doe-owned source |

## Operating rules

- Forced Doe mode must fail closed.
- Governed fallback must be explicit and typed.
- Every result must identify the browser binary and selected runtime.
- Diagnostic rows must remain separate from claimable rows.
- A public claim requires the published-browser contract, not a local smoke
  test or package wrapper.
- A release-candidate launch receipt must bind the isolated clean-install
  verifier; declared launch facts are insufficient.
- Contract changes require matching schema, checker, and fixture updates.

## Verification

Use the scripts and commands named by the milestone manifest and acceptance
contract. The manifest owns which checks are required for each milestone; this
README intentionally does not copy the changing command inventory.

`scripts/run-smoke.sh` enables Chromium's sandbox by default. It adds no unsafe
WebGPU, blocklist bypass, insecure-origin or forced ANGLE switches, and removes
Playwright's implicit unsafe SwiftShader switch. Explicit `--sandbox false` is a
diagnostic mode; it cannot establish sandboxed browser ownership. Enabled sandbox
mode rejects conflicting sandbox-disabling `--chrome-arg` values. Actual process
arguments, provider identity and execution still require their own evidence.

## Current boundary

The lane has forced-runtime contracts, diagnostics, a governed Linux package
policy, and an isolated archive verifier. The retained compact archive fails
that verifier because it is not an installable Chromium package. The lane does
not yet establish an unchanged-application release gate, broad browser
compatibility, a promoted release, or a general performance claim.

Archived Track B module designs remain historical references only. New work
requires measured benefits that justify its compatibility and maintenance costs,
plus explicit selection under GOALS.md and the milestone manifest.

### C-only Doe artifact selection

`lane-paths.sh`, `refresh-doe-app.sh` and `patch-chromium-app-doe.sh` now select
`.cache/doe-c-packages/<HEAD>/lib/libwebgpu_doe_c.<host-extension>` by default,
with `DOE_C_STAGE` selecting a completed alternative C stage. Refresh invokes
`runtime/c/tools/build-native-package.py` on committed HEAD with C/C++ tools;
existing stage directories are not overwritten (`--skip-build` reuses an
explicitly prepared stage). The C compiler shared library must remain beside the
runtime. Explicit `--doe-lib`/`FAWN_DOE_LIB` overrides remain visible and do not
cause fallback. The compatibility `--optimize` spelling accepts `ReleaseFast`
only; the C package owner controls its actual release flags.

No Zig compiler or Zig runtime is used on this default path. Historical vendored
header paths are provenance inputs, not a Zig build dependency. This selection
change and host syntax checks do not qualify Chromium integration or Metal GPU
execution; browser acceptance remains separately required.

### Relocatable Linux C-default launcher

After copying the complete Linux browser output into a separate staging
directory, install its product entry point from a completed C package stage:

```bash
python3 browser/chromium/scripts/patch-linux-browser-doe.py \
  --package-dir /absolute/staged-fawn-linux --c-stage /absolute/completed-c-stage
```

An existing `chrome-wrapper` requires explicit `--replace-wrapper`; its original
bytes are retained inside the private closure. The installer refuses active
Ninja/GN output directories and existing private closures. It checks the stage
manifest's C runtime/compiler hashes and ELF architecture, copies both libraries
and licenses under `lib/doe`, and compiles the native `chrome-wrapper` entry point.
The installation receipt binds its source, compiler command, manifest, browser
and generated launcher. This does not replace release-package preflight or
qualify the browser.

The launcher resolves all paths relative to its installed image and uses `execv`
so the browser keeps the owned PID and inherited private CDP descriptors. It
defaults to the bundled C runtime, fails if that runtime/compiler is missing, and
accepts explicit `--use-webgpu-runtime=dawn` rollback. Ambiguous/auto provider
selection and foreign library overrides are rejected. It adds no sandbox-disabling
flags. Raw `chrome` remains available for controlled diagnostics; the workspace
build profile binds that actual image separately from this product entry point.
Default profiles are private to Fawn and separate for C and Dawn under
`$XDG_CONFIG_HOME/fawn/{doe-c,dawn}` (or `$HOME/.config/fawn/...`). This prevents
Chromium's process singleton from forwarding a default launch to an already
running stock or opposite-provider profile. An explicit absolute
`--user-data-dir=/path` is preserved; use a fresh isolated profile for build
verification and close it before changing the selected provider.


The Linux C frontdoor requests early GPU sandbox startup. On X11 with ANGLE's
GLX backend, the managed pre-sandbox hook maps the fixed GLVND/Mesa library
closure without querying GLX capabilities. It admits existing DRM character
nodes only after matching the collected GPU vendor and rechecking node identity.
The broker receives exact read-only GPU metadata/topology paths and trusted
system drirc files; metadata/config writes, home configuration and unrelated
files/devices remain denied. GLX capabilities and driver helper threads start
after the sandbox seals. Native Dawn, Wayland, explicit ANGLE Vulkan/software
and late-sandbox policies keep their existing behavior.

For a Mesa host with a real X11 session and an already-built patched Chromium
checkout, the focused resource check compiles the actual owning translation
units and uses the real broker and GPU sandbox:

```bash
python3 browser/chromium/tests/test_linux_glx_resources.py build \
  --chromium-root /absolute/patched-chromium/src \
  --out /absolute/new-glx-cpu-build
python3 browser/chromium/tests/test_linux_glx_resources.py run \
  --binary /absolute/new-glx-cpu-build/controls \
  --provider /absolute/staged-fawn-linux/lib/doe/libwebgpu_doe_c.so \
  --out /absolute/new-glx-cpu-results
```

The check binds the actual DRM sysfs identity and compares unrestricted and
sandboxed accelerated GLX metadata. It creates no GL context or GPU work;
page WebGPU, native pixels, compositor output and browser acceptance require
their separate physical controls.

On that same early external X11 GLX route, a denied `sched_setscheduler` request
for another thread or process returns `EPERM`. Self and current-thread requests
retain Chromium's existing target checks; other scheduler operations and all
other launch routes retain their existing policies. This permits Mesa's optional
worker-priority request to fail without terminating the GPU process. The denied
worker keeps its inherited scheduler, which must be accounted for in any later
provider performance comparison.

Forced Doe's default Linux WebGPU backend uses the provider's Vulkan adapter
independently of Chromium's compositor API. Shared images travel through the
Skia staging owner, including its synchronization, format conversion and access
lifetimes. Chromium keeps its existing compositor; adapter blocklists and device
requirements still apply. Native Dawn retains its Vulkan-context/interop gate,
and explicit backend requests keep their existing selection.

The focused default-selector check compiles the complete decoder for the pinned
Linux and arm64 Mac configurations and executes the policy's CPU controls:

```bash
python3 browser/chromium/tests/test_linux_webgpu_backend_policy.py \
  --chromium-source /absolute/patched-chromium/src \
  --out /absolute/new-default-backend-cpu-results
```

These controls verify default selection, separately from physical adapter
availability, page WebGPU, presentation and release acceptance.

The CPU scheduler check compiles the real GPU policy, its factory and header
consumers, then installs that policy in owned fixture processes:

```bash
python3 browser/chromium/tests/test_linux_sched_targets.py build \
  --chromium-root /absolute/patched-chromium/src \
  --out /absolute/new-scheduler-cpu-build
python3 browser/chromium/tests/test_linux_sched_targets.py run \
  --binary /absolute/new-scheduler-cpu-build/controls \
  --out /absolute/new-scheduler-cpu-results
```

It verifies allowed self/current-thread requests, denied sibling/foreign requests
with unchanged target priorities, and fatal controls for the default policy,
unrelated affinity operation and other launch routes. It creates no GL context
or GPU work and does not establish browser acceptance.
