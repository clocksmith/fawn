# Native Metal phase diagnostic

Retained for the existing [native phase inventory](../../families/phase-metal.md).
[Summary](summary.json), [raw processes](raw.jsonl.gz), [frozen contract](contract.json.gz)
and [retention/limitations](retention.json) preserve all three workloads, adverse
phases and controls. These are 18 interleaved pairs with 32 total iterations
per process, including 31 warm iterations, on Apple M3.

This single-flight diagnostic did not enforce AC, thermal or foreign-load
window gates. Its retained Dawn is `200d6641`, as recorded by the frozen
contract and prior artifact hash; it is not the `d04f88359` campaign comparator.
The execution envelopes bind harness executables rather than independently
verified loaded provider binaries. The contract supplies additional build
context; it does not repair those historical envelope identities. Report time
is unknown; raw execution clocks remain unchanged.

The contract contains a descriptive JSON-pointer error. Its field values are
preserved apart from private path redactions; retention.json records the source
file hashes and correct existing `hashes` pointer. Reanalysis neither changes execution
recency nor qualifies current source. The later two-operation harness has a
separate workload and timing contract; these observations remain single-flight.
