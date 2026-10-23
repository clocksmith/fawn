# CATSCAN: Experimental C error scopes

Parent: [Experimental C migration](../CATSCAN.md)

## Target
Provide independently usable C error-scope capture and synchronous diagnostic delivery.

## Authority
- Owns per-stack filtering, first-error storage, synchronized handler replacement and synchronous dispatch.
- Does not own Zig error classification, public future delivery or device/GPU lifetimes.

## Scope
- Includes the declared C owner and explicit default-off retained-device seam in the contract.

## Contracts
- Inputs: [contract](CONTRACT.md), [source identities](SOURCE_INPUTS.json) and [header](include/doe_error_scopes.h).
- Outputs: bounded scope results and original diagnostics with explicit storage and failure ownership.

## Invariants
- Production defaults remain Zig; no hidden allocation, fallback, duplicate classifier or queue.
- Callbacks run outside the mutex with no subsequent stack access.
- Caller lifetime exclusion, final-address initialization and checked cleanup remain explicit.
- Unknown completion and physical backend obligations stay with existing owners.

## Acceptance
- Independently sealed semantic, ABI, fault, concurrency and actual native-consumer checks precede acceptance.
- Evidence: [evaluation obligations](EVALUATION.md); current state: [README](README.md).

## Non-goals
- Whole-file/full-C completion, package/browser or physical backend qualification, and production promotion.

## Freedom
Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
