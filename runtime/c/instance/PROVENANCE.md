# Instance 1 source provenance

SPDX-License-Identifier: MIT. Copyright (c) 2026 Doe contributors.
The repository [MIT license](../../../LICENSE) applies. No external implementation,
software or new toolchain is imported.

Accepted predecessor: `3582e86472db945cc441eedb3796ec7f1b13d179`.
Original isolated-campaign base: `665475907494d2095a760596218b4c2d363cb436`.
The [source manifest](SOURCE_INPUTS.json) names the exact baseline bytes and
separates the original 103-file trace, 14 additional dependencies and accepted
callback evidence. Every entry equals the accepted Git blob and working tree at
rebinding; unrelated active C compiler files are not written or substituted.

Derivation: native/support/doe_native_object_types.zig's instance storage;
lifecycle/doe_instance_device_native.zig's actual instance lifecycle and event
entrypoints; contracts/resource_lease.zig's atomic semantics; the accepted
C futures/callback owners and typed native adapters; conservative instance
feature/limit exports; actual surface/external-texture/pipeline/loss consumers.
All source paths and hashes are in the manifest. No queue algorithm, identity
source, external-texture registry or device/backend implementation is duplicated.

The predecessor callback [acceptance](../callbacks/RESULTS.md) and independent
[report](../callbacks/independent_tests/REPORT.md) close their own CPU/headless
scope, including the fresh futures-only canonical DSO. This neither executes
nor accepts the new instance owner. The independent evaluator must freeze its
own instance cases before candidate code. Runtime/public ABI changes are bounded
by the reviewed [contract](CONTRACT.md) and [header](include/doe_instance.h).

Original planning and revision1 draft seals remain intact in the isolated
campaign's evidence directory. The two reviewed precision fixes remain normative:
recursive ProcessEvents receives a fresh issuance boundary, and allocation/final
cleanup hooks cannot reenter or resurrect unpublished/dying instances. Leased
ordinary delivery callback reentry remains allowed. No historical source
snapshot, result or failed candidate was deleted or rewritten.
