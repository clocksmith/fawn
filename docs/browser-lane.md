# Browser lane

This is a routing note, not a task list or status log.

Fawn provides durable browser workspaces ([thesis](thesis.md)).
The [workspace contract](fawn-workspace-contract.md) governs its shared
observation, action, and recovery service above stock Chromium/CDP/Playwright.
The service has passed scoped behavioral qualification and shows complete-task
value over Playwright agent tooling only on its frozen differentiation suite;
release qualification is not established. `fawnWorkspace` in
[the strategy contract](../config/doe-product-strategy.json) binds its acceptance
steps to admitted evidence; native runtime wins are not prerequisites.

[Proof 3](thesis.md#three-proofs) selects the forced-Doe Chromium path for one
integrated comparison: stock Chromium with Dawn, the Fawn workspace with Dawn, and
the Fawn workspace with Doe, on identical browser and agent workloads, under
[the integrated comparison contract](../config/fawn-doe-integrated-comparison.json). Adopting
Chromium hooks or browser-runtime integration still requires measured benefits
that justify their compatibility and maintenance costs. Preserve WebGPU behavior through validation and
process boundaries; broader adoption also requires compatibility, hostile-input
security, graphics correctness, device recovery, maintainability, and application
benefit. Existing browser artifacts retain their separate assessments; the
strategy change grants no browser promotion.

- Product and package boundary:
  [`runtime-surface-boundary.md`](runtime-surface-boundary.md)
- Integration acceptance:
  [`../browser/chromium/acceptance.md`](../browser/chromium/acceptance.md)
- Integration-layer usage:
  [`../browser/chromium/README.md`](../browser/chromium/README.md)
- Machine-owned milestone state:
  [`../browser/chromium/bench/workflows/browser-milestones.json`](../browser/chromium/bench/workflows/browser-milestones.json)
- Live runtime and benchmark status:
  [`status/runtime-backends-and-bench.md`](status/runtime-backends-and-bench.md)
- External product comparison policy:
  [`../config/browser-product-comparison-policy.json`](../config/browser-product-comparison-policy.json)

`doe-gpu/browser` delegates to the browser's existing `navigator.gpu`. Only a
forced-Doe Chromium artifact can support browser-runtime claims.

## Native replacement acceptance

The installation-free compiler route requires a webpage and downloaded WASM.
It submits through browser-owned WebGPU. The separately authorized native-loader
investigation permits additional local software. Its
[retained evidence](../reports/browser-runtime/20261005-stock-chrome/README.md)
establishes loading and independent Doe computation inside Chrome's GPU process;
the webpage's work remains Dawn-owned. Proof 3 reopens native browser replacement
for its comparison build.

Its first gate: trace an ordinary
webpage request through device creation, shader compilation, submission, mapped
readback and destruction, all owned by Doe. Disable Doe execution as a negative
control and require that page operation to fail. Preserve validation, state
tracking, resource lifetimes and browser security responsibilities across any
procedure-table or wire integration. Loading and adjacent native computation
alone cannot satisfy this test. Live switching, preserved existing devices and
unchanged sandbox protections require their own acceptance evidence.

Keep rejected browser shader transformations closed. Compatibility acceptance
does not establish acceleration; product acceptance still requires correct,
unchanged applications receiving a repeatable material advantage.

## Four-lane Fawn-Doe experimental matrix

These retained runtime/shell experiments do not establish workspace authority,
safe external submissions or durable recovery. The prospective workspace
comparison uses the same stock browser and runtime for the shared-service
treatment and a competent Chromium/Playwright baseline. Legacy receipts retain
their original meaning; neither shell gains nor GPU recovery qualify the service.

In these retained lanes, `Fawn` names the legacy modified-Chromium shell build
used by the matrix, not Fawn's durable workspace service above stock Chromium.
The four distinct operational lanes are:

- **Lane A:** Stock Chromium + Playwright + Dawn
- **Lane B:** Fawn + Playwright + Dawn
- **Lane C:** Fawn + Playwright + Doe
- **Lane D:** Fawn Direct Protocol + Doe

These four lanes retain their causal semantics. Proof 3's arms use the workspace
service, not the legacy shell. **K0** (Cloudflare Browser Run plus Kitesurf) is
an external product comparator measured beside A/B/C/D, never a component
substitution lane; the
[comparison policy](../config/browser-product-comparison-policy.json) records its
sources, freshness rule and claim boundary.

## Frozen product suites

The shared suite covers HTML extraction, screenshots, navigation, automation
success, wall time, tokens, memory, cost, compatibility failures, unsupported
features, retries, recovery, and total task outcomes.

The differentiation suite covers persistent authentication, restart recovery,
offline local operation, WebGL, WebGPU, private state, and long-running
sessions. K0 runs only on workloads admitted by its documented product
boundary. Unsupported rows remain `ineligible`: they are retained, are never
scored as wins for the legacy Fawn shell build, and do not prove its quality or
value. That build must still pass its own application oracle,
lifecycle, and release gates.

The repository-owned K0 connector is
[`bench/fawn_matrix/k0_cli.py`](../bench/fawn_matrix/k0_cli.py). Its admission
policy is [`config/fawn-k0-workloads.json`](../config/fawn-k0-workloads.json):
Quick Actions execute HTML extraction and screenshots, while remote CDP executes
navigation and frozen automation. Every eligible binding supplies an exact
response SHA-256 oracle. The connector retains all differentiation rows as
ineligible and emits `claimAllowed: false`, `fawnCreditAllowed: false`,
`doeRuntimeCreditAllowed: false`, and `directProtocolCreditAllowed: false`.
Network execution still requires authorized Cloudflare credentials and an
unchanged externally reachable application.

### Attribution rules

A passing lane is necessary, not sufficient: adding a Chromium hook, the Direct
Protocol, or browser-runtime integration also requires measured benefits that
justify its compatibility and maintenance costs.

1. Only a B-over-A application outcome grants the legacy Fawn shell build
   standalone shell value.
2. Only a C-over-B application outcome grants DoeRuntime browser execution, and
   only for the measured tuple.
3. Only a D-over-C total task outcome credits the Direct Protocol, and only for
   the measured workload; a lower token count alone does not.
4. A material D-over-A result is an end-to-end challenger outcome only; it does
   not bypass the B/A, C/B, or D/C attribution gates.
5. A K0 result never changes the meaning of A/B/C/D and cannot promote or
   qualify the independently governed workspace service.

Contract Markdown under `browser/chromium/contracts/` owns detailed browser
artifact requirements. Do not duplicate those requirements here.
