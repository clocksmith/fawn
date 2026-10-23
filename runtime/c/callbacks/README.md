# Experimental C callback payload owner

Contract frozen after independent review. No implementation, executable tests,
archive, consumer change or migration acceptance exists for this owner yet. The
independent evaluator's inventory must freeze before implementation.

The [public API](include/doe_callbacks.h) and [ownership contract](CONTRACT.md)
move the actual allocation/publication/retirement responsibility from the retained
`doe_callback_delivery.zig`, not its per-API typed reply semantics. The
[exact selected-consumer seam](integration/CONTRACT.md) preserves the original
owner and tests and requires the already accepted C futures integration.
[Provenance](PROVENANCE.md) binds the inspected source.

The eventual C-only archive uses existing GCC and the public accepted
[futures API](../futures/include/doe_futures.h); it contains no Zig compilation,
second pending list, future implementation copy or worker. A separate selected
retained native artifact uses approved Zig for its typed adapter and consumers.
Production/default replacements remain zero. Physical backend and successful
adapter/device/resource paths remain unqualified.
