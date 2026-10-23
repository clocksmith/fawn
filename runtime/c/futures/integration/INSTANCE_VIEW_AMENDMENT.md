# Native-instance borrowed queue-view amendment

This additive experimental amendment accompanies the reviewed
[C native-instance contract](../../instance/CONTRACT.md#stable-queue-and-lease-without-a-zig-instance-shell).
The original [integration contract](CONTRACT.md) and its seal remain historical
accepted inputs; the standalone public C queue contract and implementation policy
are unchanged. No implementation acceptance is implied by this amendment.

For the new opaque instance selection, the retained adapter receives a copied
non-owning QueueView containing the actual C queue pointer and saved owner hooks,
not a pointer to a temporary view or a lifetime-owning Zig instance shell.
Completion registration snapshots that value. Readiness snapshots it before C
entry and holds an independent full-call lease until return, without rereading
a completion that its callback may free. Late dispatch still uses the current
callback at delivery, not the value at registration.

The two older selected-native combinations retain their explicitly initialized
PendingCompletions storage and delegate through a borrowed value. No queue storage
is copied or moved, no second identity domain is created, and copying a view does
not retain. The facade consistently selects the matching reference type for all
consumers. Standalone ownerless fixtures retain their explicit external lifetime
preconditions.

Every changed adapter path requires new independent regression coverage for the
futures-only and futures-plus-callback selections, as well as the new instance
selection. Historical passes do not qualify the amendment. The original Zig
future and callback owners and default linkage remain unchanged. Actual device
completion, configured surface/backend success and GPU qualification remain open.
