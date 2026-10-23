# Initial developer findings, 2026-10-23

Preserved author findings, not independent acceptance findings:

1. Initial real-source layout fixture used reserved WGSL entry name `layout`.
   Shared sema rejected it; fixture renamed to `layout_test`. Admission unchanged.
2. Invalid binding mutation failed earlier at sized resource-root admission than
   the initial expected diagnostic fragment. Test now checks that actual cause.
3. A literal negative index was rejected by shared constant-index validation.
   Fixture changed to an immutable runtime-stage local to exercise robust indexing.
4. Enabling the full shared `-Wswitch-enum` warning set found non-exhaustive
   emitter dispatch switches. Dispatch coverage was made explicit and the target
   Makefile now retains the shared warning. No warning was suppressed.

Final strict checks and ASan/UBSan passed after these corrections. Original
milestone tests, accepted receipts and shared admission code were not modified.
