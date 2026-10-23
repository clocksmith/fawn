# Post-handoff execution supplement

2026-10-23. Native source inspection has begun after seal
51347ba22f88c12724af95a6d87d821615b86782855ca8b4fa1e892cbe584669.
No native candidate execution has occurred. These are mechanical realizations
of sealed N05/B02/B03, not a new preimplementation blind fixture claim.

`default_regressions.zig` imports the original future, callback, loss, lease,
pipeline cleanup, global work-done and trace owners. Explicit names from their
pinned source form `default_regression_filters.json`; actual matched counts will
be checked, and no empty filtered run counts. None opens a device. Default test
options derive from sealed independent options with all three C flags false.
Canonical builds will use generated build options, not those fixture options.

Resource discipline: all canonical commands run sequentially, with `-j1`.
The original pre-handoff runner is retained byte-for-byte; the invocation adapter
only adds this build scheduler limit. This does not change acceptance flags or
source/test assertions.

Actual DSO trace supplement: run each independently compiled public consumer in
a fresh process with DOE_PROGRAM_IDENTITY_TRACE_PATH pointing to a new file.
Every native_object_created row must retain schemaVersion1, original traceKind,
nonzero address/extent and increasing sequence. Instance rows must use exactly
`native.support.doe_native_object_types.DoeInstance`, never an anonymous Zig alias.
FCI extent must equal the normal C instance's observed allocator extent; Z/F/FC
extents must equal selected typed layout. This tests the author-disclosed trace
name repair and does not pretend that disclosure was independently rediscovered.
