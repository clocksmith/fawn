# Two-vendor field-group reuse experiment

This is an owner-requested experiment, not a claim of real ATS qualification.
`forms.json` freezes contact controls from read-only public Greenhouse and Lever
responses. Each vendor has a learning posting and a distinct evaluation posting.
Source URLs, response digests and capture time are retained. Raw responses stay
in ignored local evidence. These are **contact-control projections**, not full
DOM snapshots: required uploads, location widgets, other questions, styling and
vendor JavaScript are excluded. Stage transitions and adversarial changes are
synthetic. Findings cannot be generalized to complete applications.

The local fixture has no remote assets or captured scripts, and CSP forbids
connections and form submissions. Task actions use the real workspace service,
fresh observations, current grants and the existing value-settling checks.
Fault injection and oracle reads are the only direct harness browser operations.

## Freeze before learning

`oracle.json` assigns meanings to immutable field IDs, independently of the
label-based replay rules in `contracts.js`. Neither planner nor cache consumes
the oracle or its field-ID map. The oracle checks every observed input/change
write and rendered review values. An email retained in the decoy field fails.
Decoy and hidden duplicate controls must remain empty.

`test-freeze.json` binds the forms, oracle, policy, group meanings and fixture
before evaluation. Each run also hashes its harness, service and client inputs
into `frozen.json` before learning and checks them again before reporting.
Choose a new output directory for every trial; preserve failures.

```sh
node browser/workspace/qualification/workflow-generalization/run.js --controlled --out bench/out/generalization-baseline
node browser/workspace/qualification/workflow-generalization/run.js --out bench/out/generalization-sonnet-baseline
```

The default arms are the unchanged whole-page cache, fresh planning and a
label-based script that tolerates reordering and explicitly reviewed aliases.
`--controlled` uses the script as a deterministic planner substitute; it measures
browser plumbing, not model quality or comparative product performance. Without
it, the fresh and cache-learning/fallback arms call real Sonnet through the
existing restricted CLI planner, under explicit request and spend bounds.
Scripted decisions are reported separately from model calls.

Evaluation freezes learned graphs: it never learns from holdouts or repairs.
All arms get matched profiles, independent workspaces, fresh grants and isolated
browser profiles. Arm order alternates by variant. The variants cover unrelated
questions, reordering, reviewed label changes, account differences, ambiguous
labels, hidden duplicates, dependent questions, stale observations, revoked
permissions, unknown outcomes, denied disclosure and a semantic decoy. Accounts
are caller-supplied test identities, not inferred signed-in identities.

## Interpretation

Each row records cached field fills and all cached steps, requested-field count,
model calls, decisions, preparation/task/complete time, completion, interventions,
unknown actions and semantic wrong-field fills. Zero submissions and zero
observed wrong-field fills are mandatory. Permission and ambiguity stops count
as safe stops, never completed forms. Uncertain effects permit no next decision
or execution. Stale never-dispatched actions may be freshly planned.

Report setup and maintenance independently from runtime. Human authoring and
repair time are not inferred from source timestamps or replaced with execution
time. Until separately timed setup/repair and a real participant's repeated use
exist, the report leaves those measurements absent and product advantage false.
This experiment does not establish population reliability or universal safety.
