# CATSCAN: Browser benchmark execution

Parent: [Benchmark and evidence system](../CATSCAN.md)

## Target
Measure browser application work with observable provider identity and completed output.

## Authority
Own page/launch orchestration, browser measurement and per-launch receipts.
Exclude runtime semantics, inventory selection and publication authority.

## Scope
Maintained browser harnesses and fixtures beneath this directory.

## Contracts
Input: [fresh-launch contract](fresh_launch/README.md) and explicit browser/library paths.
Output: individual launch receipts consumed by the existing analyzer and inventory.

## Invariants
Retain failures and exact measured artifacts; requested selection is not loaded identity.
Counterbalanced order, completion and output controls preserve comparison scope.
Receipt metadata runs outside measurement; unknown provenance stays explicit.
Partial local runs remain diagnostic and cannot qualify other platforms.

## Acceptance
Schedule, failure propagation and receipt controls pass without requiring GPUs.
- Evidence: [browser harness tests](../tests/test_browser_fresh_launch.py).

## Non-goals
A second workload registry or native-runtime qualification.

## Freedom
Implementation varies while preserving measurement contracts.
