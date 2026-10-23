# Review handoff

Packet is complete for independent design review, not implementation admission.
DRAFT.sha256 seals exact author-review inputs; it is not an accepted/frozen
contract or independently authored case seal. No repository edit or build ran.

Recommended decision: no-heap caller storage, private actual typed C bridge,
one C mutex, explicit final-address init and checked final deinit. C owns scopes;
all six classifier callers retain one original Zig classifier. Standard future/
payload delivery and device/GPU references do not move. Callbacks may reenter or
release their last same-thread owner; no post-callback stack access is allowed.

Reviewer questions/gates:
1. Accept eight-function API with explicit status plus original pthread code,
   cleanup-only retry and diagnostic termination on uncertain unlock failure?
2. Accept post-create/pre-publication initialization and cold ReleaseInternal
   rollback, without claiming unexecuted physical constructor cleanup?
3. Confirm exact C/Zig callback ABI proof strategy, including the two narrowly
   declared native legacy callback signatures, private layout probes and executed
   bidirectional callback cases. No unrelated pointer/function casts are allowed.
4. Independently freeze expectations and fault/concurrency/negative controls
   before implementation. Author EVALUATION.md is only a proposed obligation set.
5. Reconcile dirty shared build/source-layout/inventory/proc-manifest inputs with
   their current owner; reread and rebind exact final sources before contract seal.

Observed source facts: retained owner260 physical lines, 33 named retained tests,
47 direct .error_scopes accesses, six production shared-classifier calls and 24
validation names. The 22 device-construction paths are lexical audit leads, not
complete AST reachability or executed-test counts. SOURCE_INPUTS is authoritative
for exact hashes, dimensions, rows and dirty-input identity.

No physical backend, package, browser, production-default, complete-file C
replacement, 50% reduction or race-freedom result is claimed. Inherited Node addon
routing PREFERS standard pop when present but gives it the native callback shape;
it is not merely a fallback and stays explicitly unqualified, not silently fixed.
