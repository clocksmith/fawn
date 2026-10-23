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
