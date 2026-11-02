# CATSCAN: Benchmark and evidence system

Parent: [Doe](../CATSCAN.md)

## Target

Convert governed correctness, reliability, comparability, and performance observations into typed evidence.

## Authority

- Owns executors, oracles, comparisons, gates, tools, artifacts, and evidence assembly.
- Excludes strategy, runtime semantics, package APIs, and hardware behavior.

## Scope

- Includes this directory; children narrow scope.

## Contracts

Inputs:
- Workload law: [`../docs/workload-system.md`](../docs/workload-system.md).
- Performance contract: [`../docs/performance-strategy.md`](../docs/performance-strategy.md).
- Package full-buffer float32 alternatives:
  [`plans/package_readback_expectation.schema.json`](plans/package_readback_expectation.schema.json).
  Validator requires versioned finite per-value alternatives and exact byte count;
  prospective CPU plans never qualify historical prefix captures.

Outputs:
- Hash-bound run, compare, claim, replay, failure, and release-admission artifacts.
- Registered cube inventory and CURRENT preserve executions and scoped selections.

## Invariants

- Correctness and structural equivalence precede performance interpretation.
- Missing work, asymmetric timing, hidden fallback, or failed oracles cannot become wins.
- Metal setup cohorts include provider/framework loading inside setup on both
  providers; preloaded framework work cannot qualify that boundary.
- Diagnostic evidence cannot be relabeled to promote a claim.
- Recency requires execution receipts; unknown machines never merge by profile.
- Three-provider package comparisons bind loaded addons, enforced backend, physical
  adapter and per-worker completion; public operations require full output validation.
- Sole-adapter experiments require before/after hardware queries and stable native
  identity. Reject ambiguity; display names cannot substitute. Historical verdicts stand.
- Output proof belongs to its execution; preflight/replay cannot qualify timed samples.

## Acceptance

- Blocking gates enforce correctness and evidence policy.
- Evidence: [`runners/run_blocking_gates.py`](runners/run_blocking_gates.py).

## Non-goals

- Leaderboards or application-adoption substitutes.

## Freedom

Preserve contracts and acceptance.
