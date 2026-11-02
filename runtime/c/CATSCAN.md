# CATSCAN: C compiler and runtime

Parent: [Runtime](../CATSCAN.md)

## Target

Own the default-selected C compiler and runtime, with independent consumer/backend qualification and retained Zig reference and explicit legacy routes.

## Authority

- Owns C implementations and explicit native boundaries under this directory.
- Does not select production providers, relax acceptance or inherit hardware qualification.

## Scope

- Includes the staged C migration; the compiler child owns source meaning and target artifacts.
- The [modules owner](modules/CATSCAN.md) preserves host policy models and numeric service contracts.

## Contracts

- Inputs: [migration gates](../../docs/c-migration/MIGRATION.md), original subsystem contracts and source-bound tests.
- Outputs: independently buildable C artifacts, explicit unsupported behavior and reproducible acceptance evidence.

## Invariants

- Default selection is not qualification; retain Zig reference/legacy consumers and historical evidence until each retirement is proven safe.
- No hidden Zig generation, compiler invocation, linking fallback or runtime delegation in the C path.
- Explicit ownership, checked size arithmetic, deterministic diagnostics and original source attribution remain mandatory.
- CPU simulation and target validation do not establish physical hardware qualification.

## Acceptance

- Each owner passes its declared contract, independent tests, native consumption and applicable lifecycle/backend gates before being marked migrated.
- New work follows the [common C guide](STYLE.md) and prospective
  [quality/size contract](../../docs/c-migration/QUALITY_TARGET.md); size never
  substitutes for feature, readability or correctness acceptance.
- Evidence: [migration manifest](../../docs/c-migration/MIGRATION.md).

## Non-goals

- Mechanical line translation, macro frameworks, fake backends or unsupported qualification claims.

## Freedom

Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
