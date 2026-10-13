# Doe product strategy contract

`config/doe-product-strategy.json` is the schema-validated projection of
[`thesis.md`](thesis.md), beneath the durable goals in [`GOALS.md`](../GOALS.md).
It is the machine-readable source for milestone order, the binding proving set,
and development-effectiveness recording. Status routes evidence; it does not
maintain another roadmap.

## Migration to the integrated Fawn workspace

Schema version 7 and strategy version 6.0.0 select Fawn as the integrated
human/agent durable browser-work product, beginning above stock Chromium with
narrow CDP/Playwright adapters. Its admission no longer depends on native runtime
wins. `fawnWorkspace` adds the canonical ordered service, observation/action,
ownership, recovery, reusable-workflow and selective-capability steps. The
[workspace contract](fawn-workspace-contract.md) owns their behavioral requirements.
`surfaces.fawn` becomes `primary-product` / `durable-browser-work`.

This is a prospective contract migration, not service implementation. Every
workspace step remains not implemented and unestablished; the schema rejects
completed or qualified assessments until an implemented evidence contract and
semantic validator are deliberately admitted. Workspace release claims remain
blocked. [Workspace status](status/fawn-workspace.md) records missing boundaries;
schema tests cannot prove dispatcher behavior or durable recovery.

Native milestone IDs/order, backend focus, numerical requirements, existing
ONNX evidence, and historical browser M0–M6 receipts retain their meaning.
The `executionOwnership` browser-selection field still governs optional runtime
replacement, not the workspace above Chromium. Independent compiler/runtime
adoption remains independent of Fawn and Doppler.

Browser comparison policy version 3 selects workspace implementation rather than
runtime-first adoption as its work trunk and adds a prospective competent-stock-
Chromium/Playwright workflow comparison. Historical A/B/C/D substitution lanes
and K0 eligibility remain unchanged. Existing fixture, shell, GPU-recovery and
artwork results cannot qualify workspace ownership or external-action safety.

## Prior migration to independent compiler and runtime adoption

Schema version 6 and strategy version 5.0.0 explicitly distinguish independently
adoptable WGSL compilation and native WebGPU execution. Either may earn adoption
without the other; each requires separate evidence. `surfaces` adds `doecompiler`
as a primary surface with compiler-specific acceptance. This is an intended
adoption boundary, not a claim of a released standalone compiler package.

The existing ordered milestone IDs remain stable: ordinary application advantage,
independent framework integration, binding transfer, and optionally selected
Chromium integration. These are acceptance dependencies, not a requirement to
postpone correctness, packaging, or consumer discovery until a performance win.
The ONNX milestone now builds on established pinned execution, lifecycle checks,
and local isolated package acceptance. Broader compatibility, external deployment
reproduction, material replacement value, and retained use remain unestablished.
Start at the [installation guide](onnx-vulkan-installation.md) and
[delivery report](../reports/releases/20261009-onnx-vulkan-evaluation/README.md).

`executionOwnership.workSelection` replaces fixed percentage allocations and the
minimum application count with bounded batches, Vulkan focus, explicit Metal
handoff, deferred D3D12, and optional browser selection. Commercial emphasis is
independent installation, one real consumer requirement, and retained use.
Supported embedding, binaries, and maintenance remain commercial hypotheses;
payment is not a prerequisite for technical progress.

`comparisonLaw` now names the comparison classes and their measurement authority.
This is a prospective policy clarification, not a benchmark harness migration.
Existing workload schemas, checks, thresholds, and rejected results are unchanged.
A new campaign must implement any required contract/check extensions before
claiming application-level elimination. No runtime API or numerical behavior
changes. The prior schema migration and removed milestone IDs remain historical;
old receipts never acquire new meanings or promotion.

## Canonical policy references

The strategy's `surfaces` projection retains the independently adoptable compiler
and runtime, integrated Fawn, supporting DoeProof, and the DoeLab operating
model. Product goals and constraints live in [GOALS.md](../GOALS.md), rationale
and sequencing in [the thesis](thesis.md), and enforceable system invariants in
[INTENT.md](../INTENT.md). This migration guide does not redefine them.

- The [runtime ownership decision](runtime-ownership-decision.md) owns I0, I1,
  W0, D0, and credible eligible P0 controls, application/runtime attribution, and
  adoption admission. W0 qualification is not runtime adoption, and D0 testing
  does not require a preceding paid DoeProof engagement.
- The [performance contract](performance-strategy.md#comparison-classes) owns
  unchanged-application replacement versus fixed-operation/shader experiments,
  work equivalence, complete boundaries, and comparison audits. The schema
  projects these rules; unchanged historical gates and verdicts retain meaning.
- Backend work follows [GOALS.md](../GOALS.md#strategic-constraints); support and
  adoption evidence remain separately admitted for each host and backend.
- The browser comparison policy preserves A/B/C/D causal meanings and separate
  K0 eligibility. Version 3 adds workspace admission without changing those comparisons:
  B-over-A grants no C-over-B credit; D-over-C isolates Direct Protocol;
  ineligible K0 work never becomes a Fawn win. `doe-gpu/browser` remains an
  incumbent wrapper until Doe occupies the Chromium seam.

## Evidence custody and current state

Customer content never crosses products by default. Customer-derived knowledge
requires explicit authorization; only sanitized failures or reproducible backend
defects enter shared learning under the declared custody rules.

The strategy records intended admission rules, not achieved support or adoption.
[`doe-support-matrix.md`](doe-support-matrix.md), `reports/claim-index.json`, and
existing Chromium milestone artifacts remain their evidence authorities. Workspace
requirements and missing acceptance have their separate status owner. Reordered milestones
retain their unestablished or diagnostic assessments. No result is promoted by
this intent change.

Validate with:

```bash
python3 bench/gates/schema_gate.py
python3 bench/gates/catscan_gate.py
python3 -m unittest bench.tests.test_config_schemas bench.tests.test_browser_product_comparison_policy bench.tests.test_fawn_workspace_strategy bench.tests.test_doc_link_coverage
```

The policy tests reject loss of the independent compiler role, implicit backend
expansion, missing comparison classes, and reordered acceptance milestones.
Historical evidence and numerical thresholds remain untouched. Schema and
component checks validate this strategy migration; GPU qualification is separate.

The independent runtime operating objective retains Doppler-driven Vulkan
engineering. Existing
fields and ordered adoption milestones are unchanged: independent correctness
evidence does not require an external customer. The published ONNX evaluation
remains available while current-source generation correctness and dominant costs
guide native implementation. The workspace roadmap proceeds independently; no
historical verdict is changed.
