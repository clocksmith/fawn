# Experimental C error scopes

Contract-only, independently reviewed specification. No C implementation, build
or acceptance is claimed; production defaults remain retained Zig. Implementation
begins only after independent evaluator cases bind the installed contract seal.

- [Contract](CONTRACT.md) and [public declarations](include/doe_error_scopes.h).
- [Author acceptance obligations](EVALUATION.md), not independent test results.
- [Provenance](PROVENANCE.md), [source identities](SOURCE_INPUTS.json), and
  [review dispositions](REVIEW.md).
- [Style](STYLE.md), [charter](CATSCAN.md), and [installed seal](CONTRACT.sha256).
- [Unchanged original draft seal](review/original-draft/DRAFT.sha256).

The planned C owner uses explicit caller storage and one mutex. Native adoption
requires final-address initialization and checked teardown. Classification remains
one retained Zig owner with six actual consumers; public callback/future queues
and device/GPU lifetime ownership do not migrate here. Shared candidate3 route
inputs remain held and unaccepted. CPU scope tests cannot qualify hardware,
package/browser adoption, full-C completion or a production-default change.

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
