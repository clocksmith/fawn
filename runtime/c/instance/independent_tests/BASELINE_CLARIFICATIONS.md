# Pre-candidate baseline clarifications

2026-10-23: CASES seal 1 remains intact. Its BASELINE_CHECK reports exactly one
worktree mismatch: docs/c-migration/MIGRATION.md, changed by lead specification
installation while the evaluator was sealing. All 123 accepted Git blobs match.
Installed specification commit is 6baac036a980981ac94328d1fe9d751e85cf54ea;
INSTANCE_VIEW_AMENDMENT.md has SHA256
01e84a49e3f0b62f67361e31b3396a76408f1d76130ed3444413f64a7b9bbc98.

Reading the accepted baseline queue storage implementation (not candidate code)
clarifies C06: a single pthread_condattr_destroy failure after successful setup
returns CLEANUP_REQUIRED immediately, so it belongs in terminal C07, not ordinary
rollback. Mutex-init, condattr-init, setclock and cond-init failures return ordinary
SYSTEM_ERROR if their real rollback succeeds. A failure during rollback may
instead return CLEANUP_REQUIRED. This corrects the inventory's imprecise mention
of a combined fault without changing the frozen contract or relaxing acceptance.

C14 direct standalone readiness requires the test producer's explicit full-call
lease around accepted doe_future_mark_ready; the public C instance API contains
no readiness wrapper. N01 independently covers the real selected Zig Completion
markReady lease, rather than inferring it from that C producer fixture.
