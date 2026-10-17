# Chromium integration layer

`browser/chromium/` owns Doe's repository-local Chromium integration contracts,
scripts, milestone checks, and diagnostic artifacts. It is not the Chromium
checkout itself and it does not change the public `doe-gpu/browser` contract.

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
- Core native runtime behavior belongs under `runtime/zig/`.
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

Use `scripts/run-smoke.sh --sandbox true` for sandboxed runtime diagnostics.
The smoke runner retains its sandbox-disabled legacy default for historical
comparisons; those runs cannot establish sandboxed browser ownership. Explicit
sandbox mode rejects conflicting sandbox-disabling `--chrome-arg` values.

## Current boundary

The lane has forced-runtime contracts, diagnostics, a governed Linux package
policy, and an isolated archive verifier. The retained compact archive fails
that verifier because it is not an installable Chromium package. The lane does
not yet establish an unchanged-application release gate, broad browser
compatibility, a promoted release, or a general performance claim.

Archived Track B module designs remain historical references only. New work
requires measured benefits that justify its compatibility and maintenance costs,
plus explicit selection under GOALS.md and the milestone manifest.
