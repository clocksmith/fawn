# Experimental C label-store contract

This is a source/header contract-only snapshot, not an implementation or accepted
migration. [CONTRACT.sha256](CONTRACT.sha256) binds the reviewed
[ownership contract](CONTRACT.md), [public header](include/doe_labels.h),
[private native boundary](include/doe_labels_native_private.h),
[retained seam](integration/CONTRACT.md) and evaluation/source records.
Independently authored cases must be sealed before implementation or executable
test realization. No build command or test pass exists for this owner yet.

The proposed C owner copies arbitrary label bytes, preserves the old label on
failed replacement and returns explicitly borrowed views. A view is not safe
across concurrent replacement/removal after the store unlocks. External lifetime
and invalidation exclusion remain required. Cleanup rejection-only probes are
defined; only exclusive destroy retry recovers a store after cleanup failure.

The real retained-consumer route uses one default-off facade for both direct
imports and preserves the existing three exported label symbols. It includes the
accepted C-instance cleanup callback without changing instance ownership.
[SOURCE_TRACE.md](SOURCE_TRACE.md) enumerates actual callers. The separately
proposed surface-release cleanup repair must first demonstrate its inherited
failure through an independent CPU regression; it is not implemented here.
External-texture/shared-buffer no-op setters and package device-label stubs remain
unqualified, not completed label semantics.

[SOURCE_INPUTS.json](SOURCE_INPUTS.json) binds 63 observed inputs, not a full
native compilation closure. [SOURCE_REBIND.json](SOURCE_REBIND.json) records the
sole accepted M12 documentation delta. Current route/shared producers remain
candidate3-bound and explicitly unaccepted. [DRAFT_PACKET_PRIOR.sha256](DRAFT_PACKET_PRIOR.sha256)
preserves the original historical draft seal verbatim; its old filenames are not
the active installed contract and must not be used as the current verification list.

Follow [common C style](../STYLE.md), [owner style](STYLE.md) and the
[author evaluation requirements](EVALUATION.md). The independent evaluator owns
its own cases and verdict. No production-default, package/browser, complete
migration, performance, size-reduction or GPU qualification is claimed.
