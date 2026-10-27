# CATSCAN: Benchmark and evidence system

Parent: [Doe](../CATSCAN.md)

## Target

Execute governed workloads and convert correctness, reliability, comparability, and performance observations into typed evidence.

## Authority

- Owns workload executors, oracles, comparisons, gates, tools, raw artifacts, and evidence assembly.
- Does not own product strategy, runtime semantics, public package APIs, or hardware behavior.

## Scope

- Includes files beneath this directory except child-chartered components, which narrow this authority.

## Contracts

Inputs:
- Workload law: [`../docs/workload-system.md`](../docs/workload-system.md).
- Performance contract: [`../docs/performance-strategy.md`](../docs/performance-strategy.md).
- Package full-buffer float32 alternatives:
  [`plans/package_readback_expectation.schema.json`](plans/package_readback_expectation.schema.json).
  The package plan validator enforces versioned explicit finite per-value
  alternatives and exact byte count; prospective CPU reference plans never
  retroactively qualify retained prefix captures.

Outputs:
- Hash-bound run, compare, claim, replay, failure, and release-admission artifacts.

## Invariants

- Correctness and structural equivalence precede performance interpretation.
- Missing work, asymmetric timing, hidden fallback, or failed oracles cannot become wins.
- Diagnostic evidence cannot be relabeled to promote a claim.
- Optional three-provider package baselines bind actual loaded addons, enforced backend selection, physical adapter identity and per-worker completion before comparing full output-validated public API operations.
- Output proof belongs to its actual execution. Separate preflight or replay
  evidence cannot be assigned to timed samples or admit their comparison.

## Acceptance

- Canonical blocking gates execute the registered correctness and evidence policy.
- Evidence: [`runners/run_blocking_gates.py`](runners/run_blocking_gates.py).

## Non-goals

- A benchmark leaderboard or a substitute for application adoption.

## Freedom

Any mechanism is permitted if it preserves these boundaries and passes the acceptance evidence.
