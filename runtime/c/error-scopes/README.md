# C error scopes

This implemented C owner supplies error-scope storage to the default-selected
native frontend. [Public push/pop conversion](../native/src/errors.c) delegates
to its typed API; [native source composition](../native/sources.mk) includes its
implementation. Default selection does not establish independent acceptance,
backend qualification or complete WebGPU parity.

- [Contract](CONTRACT.md) and [public declarations](include/doe_error_scopes.h).
- [Author acceptance obligations](EVALUATION.md), not independent test results.
- [Provenance](PROVENANCE.md), [source identities](SOURCE_INPUTS.json), and
  [review dispositions](REVIEW.md).
- [Style](STYLE.md), [charter](CATSCAN.md), and [installed seal](CONTRACT.sha256).
- [Unchanged original draft seal](review/original-draft/DRAFT.sha256).

The C owner uses explicit caller storage and one mutex. Native adoption requires
final-address initialization and checked teardown. The native frontend converts
public errors into the scope API; callback/future queues and device/GPU lifetimes
stay with their existing owners. The explicit retained-Zig integration uses the
[private bridge](integration/doe_error_scopes_zig_bridge.h) and its archive recipe.

The installed contract seal and original review describe their bound historical
source. They remain unchanged. Their original contract-only and candidate-route
status does not describe the current implementation; current host checks and
source-bound receipts retain their separate scopes.

## Current host primitive source extension

The implemented stack uses the private [host primitive owner](../platform/README.md)
for its sole mutex. Filtering, first-error storage, synchronous callback dispatch
outside the lock, argument precedence, errno restoration and retryable exclusive
cleanup remain here. The Win32 source port returns original Win32 error bits in
the existing `system_error` field; success and failure are determined by status,
not the sign of that field. POSIX callers retain the original pthread codes.
No public layout, status, ABI version, callback rule or allocation policy changes.

`make check` keeps the actual GNU mutex fault controls unchanged; `make check-api`
is the portable C/C++ API route. Archive manifests bind all shared primitive
headers. Actual SDK compilation/CPU execution is separate from historical Linux
acceptance and does not qualify Windows public frontend or GPU execution. The
original contract/seals remain historical evidence for their bound source.
