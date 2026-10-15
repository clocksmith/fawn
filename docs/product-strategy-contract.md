# Doe product strategy contract

`config/doe-product-strategy.json` is the schema-validated projection of
[`thesis.md`](thesis.md), beneath the durable goals in [`GOALS.md`](../GOALS.md).
It is the machine-readable source for milestone order, the binding proving set,
and development-effectiveness recording. Status routes evidence; it does not
maintain another roadmap.

## Model-driven comparison frozen

The browser comparison policy moves to schema version 9
(`fawn-external-product-comparison-v9`) and adds
`workspaceComparison.modelDrivenDifferentiation`, frozen before implementation.
It pins:
- one model, sampling, token budget and turn limit for both sides
- the system prompt, task prompts, agent-facing tool descriptions and held-out
  workflow specifications, by hash
- the browser binary

Interruption recovery and scoped observation are advantage requirements;
held-out workflows and the stale-decision and handoff scenarios are
non-inferiority checks. Running it needs model API credentials and approval of
the spend. No strategy state changes.

## Stronger-control and compatibility evidence

Schema version 13 and strategy version 6.6.0 add two required blocks:
- `fawnWorkspace.strongerControlEvidence` binds the full second-suite result:
  the helpdesk against revalidating Playwright agent tooling. Its frozen rule
  passed with two areas, and the first suite was a clean prior.
- `fawnWorkspace.compatibilityEvidence` binds admitted runs of the frozen case
  set on further browser builds, currently installed Google Chrome stable.

All evidence blocks now name commit `9f522f7d9`. Step 5's label is unchanged and
the second suite only narrows it: the remaining advantages are durable outcomes
across interruption and region-scoped observation. The second suite's status
becomes `run`. Release admission stays blocked.

## Second comparison frozen against a stronger control

The browser comparison policy moves to schema version 8
(`fawn-external-product-comparison-v8`) and adds
`workspaceComparison.revalidatedDifferentiation`, frozen before implementation.
It runs on a different application, a helpdesk. Its control is Playwright agent
tooling that revalidates every decision's inputs with a fresh snapshot before
mutating, and cancels a pending call and observes again after human control. It
keeps the four target areas with their own requirements. Its product-advantage
rule also requires no regression in the first suite. A model-driven stage is
recorded as planned and needs its own freeze. No strategy state changes.

## Product advantage bound to the frozen differentiation result

Schema version 12 and strategy version 6.5.0 add the required
`fawnWorkspace.differentiationEvidence`. It names:
- the frozen suite and the full differentiation result
- the scripted comparison rerun on the same commit
- the source commit
- the scope: the frozen differentiation suite against Playwright agent tooling

Step 5's assessment becomes `advantaged-on-frozen-differentiation-suite`. The
policy tests require that result to be full, for the named committed source,
with the frozen product-advantage rule passed. They also require its
requirements to match the frozen ones, no material regression in the suite or
the scripted comparison, and one browser binary with no debugging endpoint.
`qualificationEvidence` now binds the behavioral verdict for the same commit.
The suite's status becomes `run`. Release admission stays blocked, and broad
superiority, compatibility beyond the fixture and release qualification remain
unestablished.

## Frozen differentiation requirements

The browser comparison policy moves to schema version 7
(`fawn-external-product-comparison-v7`) and adds
`workspaceComparison.differentiation`, frozen before implementation. It names:
- the agent-workflow scenarios
- the control: Playwright agent tooling with page-wide incremental snapshots,
  aria-ref actions, a browser host that outlives the agent, checkpoints,
  idempotency and application-state checks, and pause-then-resume human control
- the shared conditions and think time
- one improvement requirement per target area
- the product-advantage rule

No strategy state changes; step 5 stays unestablished until that rule passes.

## Behavioral qualification bound to admitted evidence

Schema version 11 and strategy version 6.4.0 bind workspace assessments to
evidence. `fawnWorkspace.qualificationEvidence` names:
- the frozen [case set](../config/fawn-workspace-qualification.json) and the
  [validator](../browser/workspace/qualification/validate.js)
- the admitted verdict and comparison under `reports/fawn-workspace/`
- the source commit they ran
- the scope: the frozen case set on the recorded browser and host, which is not
  release qualification

Steps 1–4 become `implemented-behavior-qualified` with assessment
`established-on-frozen-case-set`; the policy tests require each of their
requirements to be established in that verdict. Step 5 becomes
`implemented-compared`, and its assessment stays `unestablished`. The overall
status becomes `partially-implemented-behavior-qualified`. Release admission stays
blocked, and claimable or release-qualified states remain rejected.

The browser comparison policy moves to schema version 6
(`fawn-external-product-comparison-v6`). `workspaceComparison` freezes:
- the runner and the competent control implementation
- the shared browser runtime, the control's state-handling capabilities and the
  equal opportunities
- the scenarios with their oracles and repetitions, and the excluded handoff
  scenario with its reason
- the measures and the material criteria

The workspace service config moves to schema version 2 and protocol 2: observation
regions, regions-scoped actions, link-destination authorization, full-text
element fingerprints and `element-text-excludes`. Protocol 1 journals replay
unchanged.

## Hardware-first initial target and complete-system comparison

Schema version 10 and strategy version 6.3.0 adopt a hardware-first entry point
without changing Fawn's goal. `positioning.initialDeploymentStrategy` records
hardware-first local execution as the initial deployment strategy, not a mandatory
dependency or universal performance claim. Doe enters only through explicit
integration with independent qualification. The `amd-vulkan` platform lane gains
`initialQualificationTarget`, which binds host profile `linux_strix_halo_radv` and
the [frozen host record](../reports/benchmarks/amd-vulkan/20261014-initial-qualification-target/README.md).
The schema fixes its label, keeps it `unqualified-until-applicable-acceptance-passes`,
limits existing evidence to recorded coverage, and keeps system RAM, GPU
allocation limits and measured residency separate. `comparisonLaw.categories`
separates provider attribution (Doppler+Doe versus Doppler+Dawn, which isolates
the provider) from the complete-system alternative (which does not).

The browser comparison policy moves to schema version 5
(`fawn-external-product-comparison-v5`) and adds `alternativeCompleteSystem`.
This is ordinary Chromium plus a native inference engine, separate from the
A/B/C/D lanes, K0 and provider attribution. It requires matched task quality,
context, output requirements, concurrency and resource budgets, and records
differing quantization, model format, backend and engine build. N0 pins one
llama.cpp build (commit, build options, Vulkan backend) and one hash-pinned GGUF
model, and has not run. Ollama (N1) and LM Studio (N2) remain unassessed
candidates until their execution paths and workload eligibility are established.
No lane, comparator, milestone or assessment changes meaning.

## Implemented, unqualified workspace machinery

Schema version 9 and strategy version 6.2.0 record that the shared workspace
service exists in [`browser/workspace`](../browser/workspace/README.md).
`fawnWorkspace` adds the required `implementationPath` and
`developmentTestsPath` fields. Its `implementationStatus` becomes
`partially-implemented-unqualified`, and steps 1–4 (service, observation/action
binding, durable ownership, uncertainty recovery) become
`implemented-unqualified`. Steps 5–6 stay `not-implemented`. Every
`currentAssessment` remains `unestablished`, release admission remains blocked,
and the schema still rejects implemented-as-qualified, claimable or qualified
states. Development tests do not admit evidence; that still requires the frozen
behavioral gates, the competent-baseline comparison and a semantic validator.

## Alignment to the agent-environment goal

Schema version 8 and strategy version 6.1.0 projected the current goal: Doe builds
the environment where agents work; Fawn provides durable browser workspaces;
DoeRuntime supplies independently useful GPU execution; DoeLab tests
improvements; DoeProof records evidence. `positioning` and the surface jobs carry
that wording. Surface ids, roles and the independent compiler surface are
unchanged.

The final `fawnWorkspace` step replaces
`chromium-hooks-require-unavailable-authority-visibility-or-efficiency` with
`chromium-hooks-and-runtime-integration-require-measured-benefits-justifying-compatibility-and-maintenance-costs`.
Chromium hooks and browser-runtime integration are added only when measured
benefits justify their compatibility and maintenance costs; a capability already
available through existing interfaces is not such a benefit. This schema change
renames one required-evidence id; no field is added or removed.
`independenceBoundary.rule` and `externalProductComparison.decisionRules` carry
the same rule in prose, and one decision rule is added.

`executionOwnership.operatingObjective` and the first two milestone outcomes name
the runtime focus: one correct, reproducible Doppler Node generation advantage
over Dawn on Vulkan, then ONNX transfer. The second outcome is conditional on the
first: no Doppler correction is accepted yet. The fourth milestone's outcome and
its application-benefit evidence entry now require measured benefits that justify
compatibility and maintenance costs. Milestone ids, order, assessments and all
other required evidence are unchanged, and nothing is promoted.

The browser comparison policy moves to schema version 4
(`fawn-external-product-comparison-v4`). Its shape is unchanged, but its meaning
is not: two fork conditions and one added stop condition carry the
measured-benefit requirement, the DoeProof interpretation says it records
evidence, and prose that meant the modified Chromium binary now says legacy Fawn
shell build. Lane ids, lane order and the pinned lane labels are unchanged.

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
  ineligible K0 work never becomes a win for Fawn or the legacy Fawn shell build. `doe-gpu/browser` remains an
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
