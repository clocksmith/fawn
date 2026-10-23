# Typed scalar expressions and immutable locals: accepted scope

The [frozen M4 contract](MILESTONE_4.md) is accepted on candidate01 without an
implementation repair. General parsed/typed expressions, scoped immutable values,
checked integer-rational decimal conversion and SPIR-V arithmetic/max emission
admit unchanged scaling and ReLU sources. Production Zig, the public ABI/header,
CLI ownership and original compilation/publication owner are unchanged.

## Evidence

Independent source-built GCC evaluation passes 164 retained +106 new positives,
226 retained +69 new negatives and 14 individually justified old-negative lifts.
Each lift is tied to the frozen clause and independent buffer effects, not simply
changed to pass after the candidate accepted it. Tests include 2,800 concurrent
calls, 17,625 black-box instruction trials, exact-rational decimal/operation
oracles, opcode mutation controls, six C/C++ consumers, guard pages, 150,000
random/mutated sanitizer cases and 344,144 corpus sanitizer calls. CLI checks pass
981 cases plus 172 ordinary and 172 sanitized I/O-fault cases. ASan/UBSan are
active; LeakSanitizer is not available. Only C, C.utf8 and POSIX locales exist on
this host; all four glibc rounding modes were exercised, not an installed
comma-decimal locale.

The initial independent renamed fixture incorrectly used reserved WGSL identifier
`target`; its rejection was correct. The fixture was repaired, preserving the
first receipt. A developer oracle mutation was initially insensitive and was
corrected independently of the compiler. Neither caused an implementation change.

The [source-only independent suite](independent_tests_m4/README.md) retains the
reusable checks. All underlying stages ran independently; the combined wrapper
was checked by dry-run/syntax validation, not represented as an additional complete
campaign. Repository-location black-box/CLI checks additionally exercise the
separately built developer artifacts. No historical suite is modified.

Independent source-built artifact SHA-256:

- CLI: `61676c64a1b2f59494428da8374fa3f5ed7f3b0b980146bab5896997a40da3eb`.
- Library: `f94d546d577aaa86d1c6024f351fd00cc8ad001c659622883d4f462341010bbc`.

Developer Makefile artifact SHA-256, separately bound:

- CLI: `94448360c2409bb37a9c0c6343d8f10a20a76410c7353e9e39b4a024ed4d3154`.
- Library: `a751e4e64d261531d7b380e47ebb35da1a0652cdda9d519296ba2ddf38946412`.

These builds use different source ordering/command lines; they are not claimed
byte-identical. Exact commands and source/tool/artifact bindings remain in
`doe-c-migration-evidence-20261023/milestone4/evaluator/FINAL_REPORT.json` and the
developer receipts in that isolated evidence workspace.

## Coverage and measured cost

The [M4 ledger](../../../docs/c-migration/corpus-coverage-m4.json) audits the same
98 paths /91 contents through the unchanged real consumer, with unique output
paths and no full-gate rerun. Four paths /three unique sources now emit: residual
copies, scale and ReLU. All four validate explicitly for Vulkan 1.1 and execute
independently. The other 94 paths have 71 lexical and 23 parser first failures;
semantic/emission stages are not reached, not target failures. Old prebuilt
modules do not inflate source coverage. The [M3 ledger](../../../docs/c-migration/corpus-coverage-m3.json)
remains immutable for comparison.

GCC-O2 static compile frame is 105,328 bytes, up 16,656. Recursive expression/
primary parser frames are 624 bytes each and expression lowering is 160 bytes;
this is not a measured total stack high-water mark. The developer CLI/library
are 59,616/54,064 bytes, up 8,424/4,328. The 64 expression nodes, depth16, 16
statements, four if levels, 385 IR instructions/386 value slots and 4096 words are
explicit checked limits, not a permanent allocation-free architecture mandate.

No speed/productivity, full WGSL/compiler, MSL benchmark, runtime/application,
physical numerical/backend or whole-migration qualification follows. Complete
compiler resource/layout/entrypoint/target ownership and real runtime consumers
remain open. Zero production owners are replaced.
