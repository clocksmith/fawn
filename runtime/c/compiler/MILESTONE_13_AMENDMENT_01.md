# M13 amendment 01: live semantic traversal storage

2026-10-23. Append-only correction approved by the original contract reviewer and
migration lead during implementation. The original contract, header and SHA256
seal remain unchanged and retained. This amendment changes no public ABI, logical
capacity, semantic rule, stack ceiling or qualification requirement.

The original B-analysis ledger omitted a live owner. `sema_type_reference`,
`sema_common_type`, `sema_default_type` and `constant_materialize` use Walk[V]
while previously captured SourceReference records remain live. Target Walk[V]
is not safe at that time: relative to the shared scratch origin its start is
align8(4*(Q+7))+32D. For Q4/D16/V8 it occupies bytes560..687, overlapping
expression-reference slots70..85 when E128/Ep129. A subsequent generic type or
constant materialization can corrupt earlier resource/function identities.

Correct B-analysis is SourceReference[Ep], SourceReference[S], followed by the
maximum of U, Gscratch, and independently aligned Walk[V]. Semantic Walk uses
that analysis address. Only after successful complete sema, call-cycle checking,
closure construction and uniformity, with walk_count=0, does the pipeline rebind
to the target Walk address. Graph/uniformity do not use Walk. Target phases do
not read source-reference storage. Binding a new call restores the analysis
pointer and zero count, including reuse after a failed semantic traversal.
The fixed workspace declares this additional union member as typed WalkItem[97].
No heap, VLA, hidden storage or capacity reinterpretation is introduced.

Fixed Walk1552 fits existing U5232. The binary and illustrative sparse profiles'
Walk20496 fit U428912. Their phase maxima do not grow. Other legal queries where
Walk dominates may return a larger exact workspace requirement; they must not
be rejected or silently borrow other live pools. Actual implementation flattened
CompilerLimits104 and CompilerWorkspace472 avoid the planning probe's nested
record tail padding, reducing each planned total by8 bytes: fixed146048,
ABI4 binary5241576 and illustrative ABI5 sparse2533096. Native queries and
source-bound developer tests, not these calculations, establish actual results.

This correction is author-tested separately from sealed independent acceptance.
Publication or a focused check does not establish complete migration, runtime
adoption, physical execution or the final50% reduction target.
