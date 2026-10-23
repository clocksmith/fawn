# CATSCAN: Experimental C labels

Parent: [Experimental C migration](../CATSCAN.md)

## Target

Own copied native debug-label bytes through a reusable C store and reversible retained-consumer seam.

## Authority

- Owns lookup, byte allocation, replacement, removal and cleanup.
- Excludes object references, provider selection, callbacks, error scopes and backend execution.

## Scope

- Includes this C owner and its default-off native seam.
- Independent case sealing precedes implementation.

## Contracts

- Inputs: [ownership contract](CONTRACT.md), [native seam](integration/CONTRACT.md), [migration gates](../../../docs/c-migration/MIGRATION.md).
- Outputs: [public API](include/doe_labels.h), [private boundary](include/doe_labels_native_private.h), copied ownership and borrowed views.

## Invariants

- Failed replacement preserves old bytes and pointer.
- Borrowed dereference requires exclusion of invalidation.
- Cleanup failure retains storage until exclusive successful retry.
- Selected consumers share one artifact-local owner.
- Retained defaults, ABI and historical evidence remain intact.
- Inherited defects and excluded no-op setters are not acceptance claims.

## Acceptance

- Independent C/C++ and native lifecycle, fault, concurrency and isolation checks satisfy [evaluation](EVALUATION.md).
- Evidence: [contract seal](CONTRACT.sha256) and [source trace](SOURCE_TRACE.md); behavioral acceptance is pending.
- CPU evidence does not qualify physical execution or complete migration.

## Non-goals

- Additional registries/reference counts, implicit pointer safety, production promotion, GPU/device success or performance/size claims.

## Freedom

Any mechanism preserving these boundaries and passing acceptance is permitted.
