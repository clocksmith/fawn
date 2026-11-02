# CATSCAN: Registered benchmark inventory

Parent: [Benchmark and evidence system](../CATSCAN.md)

## Target
Make recorded measurements discoverable by workload, execution, machine and measured artifact.

## Authority
Own registration adaptation, normalized identity, current selection, lossless storage
and readable projections. Exclude measurement execution, classification thresholds
and public claim authority.

## Scope
The existing cube inventory; old imports remain compatibility facades.

## Contracts
Input: [sources](../../reports/benchmarks/sources.json),
[row schema](../../config/benchmark-cube-row.schema.json),
[cube schema](../../config/benchmark-cube.schema.json),
[policy](../../config/benchmark-cube-policy.json).
Output: generated CURRENT, family histories, compact index and optional dashboard.

## Invariants
Execution/report/freeze clocks remain separate; mtime never proves execution.
Unknown machines stay isolated. Storage interning never merges observations.
Every failure, metric, exact value, uncertainty and evidence reference survives expansion.
Latest measured and qualified remain independently scoped; another binary cannot
inherit a win. Views consume shared selections without changing classification.
Historic missing evidence remains missing; new provenance records only observed facts.

## Acceptance
Codec round trips, identity/selection regressions, generated freshness and dashboard
filter controls pass.
- Evidence: [inventory controls](../tests/test_benchmark_inventory.py)
and [view controls](../tests/test_benchmark_inventory_views.py).

## Non-goals
A second ingestion service, database, scoreboard or historical qualification rewrite.

## Freedom
Algorithms vary within these boundaries.
