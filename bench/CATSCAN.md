# CATSCAN: Benchmark and evidence system

Parent: [Doe](../CATSCAN.md)

## Target

Execute governed workloads and convert correctness, reliability, comparability, and performance observations into typed evidence.

## Authority

- Owns workload executors, oracles, comparisons, gates, tools, raw artifacts, and evidence assembly.
- Does not own product strategy, runtime semantics, public package APIs, or hardware behavior.

## Scope

- Includes this directory; child charters narrow authority.

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

## Invariants

- Correctness and structural equivalence precede performance interpretation.
- Missing work, asymmetric timing, hidden fallback, or failed oracles cannot become wins.
- Metal setup cohorts include provider/framework loading inside setup on both
  providers; preloaded framework work cannot qualify that boundary.
- Diagnostic evidence cannot be relabeled to promote a claim.
- Three-provider package comparisons bind loaded addons, enforced backend, physical
  adapter and per-worker completion; public operations require full output validation.
- Explicit sole-adapter experiments query hardware before/after measurement, require
  one adapter and stable native identity, and reject ambiguity. Display names
  cannot substitute. Historical verdicts stand.
- Output proof belongs to its execution; preflight/replay cannot qualify timed samples.

## Acceptance

- Blocking gates enforce correctness and evidence policy.
- Evidence: [`runners/run_blocking_gates.py`](runners/run_blocking_gates.py).

## Non-goals

- Leaderboards or application-adoption substitutes.

## Freedom

Mechanisms preserve boundaries and pass acceptance.
