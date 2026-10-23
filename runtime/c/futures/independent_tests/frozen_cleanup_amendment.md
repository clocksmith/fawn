# Pre-implementation contract review amendment

Received before any candidate implementation existed or was read/executed.
Final contract SHA256 f78eaa3f8b1c0e3f906dfcf900a484df8202ad3cd5c2f9a85690f585f07e4395.
Final header SHA256 64df7b659fa61de34c3f26a8a8762c70a41d3f35d301a54ffe11877f43ca3404.

New DOE_FUTURE_CLEANUP_REQUIRED = 13 is an admitted explicit failure, not success.
If initialization rollback destruction fails, output is a cleanup-only handle,
not NULL; storage cannot be reused/freed until exclusive deinit retry succeeds.
Only live initialized members can be destroyed. Each ownership flag clears only
when that member's destroy succeeds; cleanup stops at first failure. Inject
failure in attribute destruction during init and rollback, condition destruction,
and mutex destruction; preserve exact attempted/successful destroy counts, then
retry and verify normal initialization works only after successful cleanup.
Ordinary operations cannot treat cleanup-only handles as live queues. A failed
normal deinit likewise leaves cleanup-only state until retry succeeds. This
amends inventory 3, preserving ordinary setup-failure rollback/reuse expectations.

Manual/external callback extent after begin_delivery returns remains caller-owned;
active-call BUSY guarantee is for library-dispatched callbacks. Deinit can wait to
acquire its normal mutex but never waits for caller quiescence.

Agreed test-only endpoints: DOE_FUTURES_TESTING gates
DOE_FUTURES_TEST_INSTANCE_LIMIT (last default 0x7fffffff),
DOE_FUTURES_TEST_SEQUENCE_LIMIT (last default 0xfffffffe),
DOE_FUTURES_TEST_LEGACY_LIMIT (last default UINT64_MAX-1).
Use tiny 2, 3, 0x8000000000000002 endpoints to exercise production transitions;
verify override without TESTING fails compilation. No exported mutation hook and
no billions-of-calls claim. Library setup/clock/wait/destroy faults use --wrap.
