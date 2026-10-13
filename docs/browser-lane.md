# Browser lane

This is a routing note, not a task list or status log.

Fawn is the integrated durable browser-work product in the [strategy](thesis.md).
The [workspace contract](fawn-workspace-contract.md) governs its selected shared
service above stock Chromium/CDP/Playwright. Its roadmap lives in `fawnWorkspace`
in [the strategy projection](../config/doe-product-strategy.json); native runtime
wins are not prerequisites. Workspace implementation and acceptance remain absent.

Browser replacement is an optional deeper capability. Its bounded
Chromium milestone follows ordinary execution, independent framework integration,
and binding application transfer. Preserve WebGPU behavior through validation and
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
the webpage's work remains Dawn-owned. Browser replacement is deferred while
active integration returns to ONNX's procedure-table boundary.

When native browser replacement is explicitly reopened, first trace an ordinary
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

The vertical product thesis evaluates four distinct operational lanes:

- **Lane A:** Stock Chromium + Playwright + Dawn
- **Lane B:** Fawn + Playwright + Dawn
- **Lane C:** Fawn + Playwright + Doe
- **Lane D:** Fawn Direct Protocol + Doe

These four lanes retain their causal semantics. **K0** is Cloudflare Browser
Run plus Kitesurf. It is measured beside A/B/C/D as an external product
comparator and is never treated as a component substitution lane. The policy
records Cloudflare's official
[announcement](https://blog.cloudflare.com/kitesurf/) and
[documentation](https://developers.cloudflare.com/browser-run/kitesurf/), the
comparator freshness rule, fork authorities, and claim boundary.

## Frozen product suites

The shared suite covers HTML extraction, screenshots, navigation, automation
success, wall time, tokens, memory, cost, compatibility failures, unsupported
features, retries, recovery, and total task outcomes.

The differentiation suite covers persistent authentication, restart recovery,
offline local operation, WebGL, WebGPU, private state, and long-running
sessions. K0 runs only on workloads admitted by its documented product
boundary. Unsupported rows remain `ineligible`: they are retained, are never
scored as Fawn wins, and do not prove Fawn quality or customer value. Fawn must
still pass its own application oracle, lifecycle, and release gates.

The repository-owned K0 connector is
[`bench/fawn_matrix/k0_cli.py`](../bench/fawn_matrix/k0_cli.py). Its admission
policy is [`config/fawn-k0-workloads.json`](../config/fawn-k0-workloads.json):
Quick Actions execute HTML extraction and screenshots, while remote CDP executes
navigation and frozen automation. Every eligible binding supplies an exact
response SHA-256 oracle. The connector retains all differentiation rows as
ineligible and emits `claimAllowed: false`, `fawnCreditAllowed: false`,
`doeRuntimeCreditAllowed: false`, and `directProtocolCreditAllowed: false`.
Network execution still requires customer-authorized Cloudflare credentials and
an unchanged externally reachable application.

### Falsifiable decision rules

1. **If B beats A but C does not beat B:**  
   The Fawn browser shell and agent features have standalone value; DoeRuntime has not earned browser ownership.
2. **If C beats B but D does not beat C:**  
   DoeRuntime provides demonstrable browser acceleration; the Direct Protocol is unnecessary or immature.
3. **If D reduces tokens but increases total task time:**  
   Retain Direct Protocol only for workloads where context cost dominates wall-clock latency.
4. **If none beat A:**  
   Do not rationalize the result; redirect DoeRuntime focus to Node/Bun/Electron package lanes.
5. **If D materially beats A across task success, latency, memory, and recovery:**  
   Record an end-to-end challenger outcome only; it does not bypass the B/A,
   C/B, or D/C attribution gates.
6. **If K0 wins shared tasks and customers do not value differentiation:**
   Reject the measured shell-product claim; this cannot promote or qualify the
   independently governed workspace service.
7. **If B does not beat A:**
   Fawn has not earned standalone shell value.
8. **If C does not beat B:**
   DoeRuntime has not earned browser execution for that tuple.
9. **If D does not beat C on the declared total task outcome:**
   Stop funding Direct Protocol for that workload.

Contract Markdown under `browser/chromium/contracts/` owns detailed browser
artifact requirements. Do not duplicate those requirements here.
