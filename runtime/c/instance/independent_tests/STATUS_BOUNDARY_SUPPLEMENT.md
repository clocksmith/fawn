# Status/active cleanup executable supplement

These concrete expected observations were sealed in C07/C12/C13 before
implementation. This additional executable was authored after reading the exact
GCC-only core, so is disclosed as a post-handoff realization, not blind code.
It checks real active queue teardown after callback retirement (no pending node),
unexpected requirement/process statuses, all accepted wait-status translations
with real accepted queue output flags, and non-input-order wait selection.

Wrappers call the actual accepted queue before substituting a named return
status, except the requirements fault which fails before queue creation.
They do not implement a queue. The active fatal fixture deliberately omits a
full-call lease to reach the already-required BUSY diagnostic path, never to claim
such use legal or successful. No production sources are edited.
