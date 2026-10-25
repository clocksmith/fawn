# CATSCAN: C host modules

Parent: [C migration](../CATSCAN.md)

## Target
Run retained module requests through independently compiled C and preserve their attributable result contracts.

## Authority
- Owns typed module input normalization, host model calculations and module artifact publication.
- Does not own GPU execution, numeric routing policy or application rendering.

## Scope
The five retained policy models and the numeric stability matmul service, exposed through `doe-c-module-runner`.

## Contracts
- Inputs: retained [module runner](../../zig/src/cli/module_runner_cli.zig) request and policy types.
- Outputs: result JSON, canonical request/policy/result hashes, numeric receipts and trace metadata consumed by the public package.

## Invariants
- Policy models retain their deterministic cost predictions; they never become GPU timing or rendering evidence.
- Numeric service values come from actual host calculations with retained rounding and ordering.
- Missing, unknown, malformed and overflowing inputs fail visibly before publishing a result.
- JSON, SHA-256 and numeric policy decisions reuse their existing C owners. No Zig build or delegation occurs in the C path.

## Acceptance
- [Owner checks](Makefile) compare source-bound retained fixtures, reject invalid contracts and exercise real package consumers.
- Evidence: [coverage](README.md) distinguishes model semantics, host execution and physical execution.

## Non-goals
Native rendering or compute execution for the retained cost models, production provider selection and hardware qualification.

## Freedom
Internal representation may vary while preserving the contracts and acceptance evidence.
