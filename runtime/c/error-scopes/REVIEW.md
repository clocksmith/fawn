# Review handoff

Independent design review approved the packet with narrow corrections now installed.
Independent evaluator case sealing is still required before implementation.
review/original-draft/DRAFT.sha256 preserves exact original author-review inputs; it is not an accepted/frozen
contract or independently authored case seal. CONTRACT.sha256 seals the installed
contract-only packet; no implementation or build ran.

Recommended decision: no-heap caller storage, private actual typed C bridge,
one C mutex, explicit final-address init and checked final deinit. C owns scopes;
all six classifier callers retain one original Zig classifier. Standard future/
payload delivery and device/GPU references do not move. Callbacks may reenter or
release their last same-thread owner; no post-callback stack access is allowed.

Reviewed decisions and remaining gates:
1. Approved seven-function API with explicit status plus original pthread code,
   cleanup-only retry and diagnostic termination on uncertain unlock failure.
2. Approved post-create/pre-publication initialization and cold ReleaseInternal
   rollback, without claiming unexecuted physical constructor cleanup.
3. Implementation must execute the reviewed exact C/Zig callback ABI proof,
   including the two narrowly
   declared native legacy callback signatures, private layout probes and executed
   bidirectional callback cases. No unrelated pointer/function casts are allowed.
4. Independently freeze expectations and fault/concurrency/negative controls
   before implementation. Author EVALUATION.md is only a proposed obligation set.
5. Shared build/source-layout/inventory/proc-manifest inputs remain held candidate3
   and unaccepted. Their current owner must release them before integration. Only
   accepted M12 HEAD/MIGRATION identity was rebound during installation.

Observed source facts: retained owner260 physical lines, 33 named retained tests,
47 direct .error_scopes accesses, six production shared-classifier calls and 24
validation names. The 22 device-construction paths are lexical audit leads, not
complete AST reachability or executed-test counts. SOURCE_INPUTS is authoritative
for exact hashes, dimensions, rows and dirty-input identity.

No physical backend, package, browser, production-default, complete-file C
replacement, 50% reduction or race-freedom result is claimed. Inherited Node addon
routing PREFERS standard pop when present but gives it the native callback shape;
it is not merely a fallback and stays explicitly unqualified, not silently fixed.

Installed review corrections: exact ordinary argument/state validation precedence;
known DEAD/cleanup-only rejection without mutex or callback access; exclusive
deinit-only recovery; native OK/LIMIT and OK/EMPTY mappings with all unexpected
operational statuses fatal; entry errno on all callback-free nonterminal returns;
callback-induced errno retained. No semantic scope or classifier expansion.
