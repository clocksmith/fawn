# CSL C port, test results

not run, per user instruction

Behavioral parity tests were NOT RUN in this pass. No test counts are claimed.
No emitter was executed, no CSL was compiled, nothing was run on GPU or physical
Cerebras hardware, and no macOS, Windows, aggregate, or production-wiring test was run.

Compile-only checks reported by workers (parsing only, NOT behavioral passes):
- TSIR CSL: Linux gcc -fsyntax-only, -std=c11 -Wall -Wextra -Wpedantic -Werror -I src,
  over all new TSIR C files plus the test driver, re-run after the simplification refactor
  and after the KV write/read export-list fix, exit 0. This proves parsing only.
- emit/csl: Linux gcc compile, -std=c11 -Wall -Wextra -Wpedantic -Werror, all 24 object
  files (22 previous new files plus csl_emit_common.c and shared csl_text.c), 0 failures,
  no duplicate exported symbols, after the simplification refactor. This proves the
  refactored code compiles under strict flags; it does not prove parity or that emitted
  text is unchanged.
- spatial/csl: Linux gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only,
  over all csl_spatial_*.c plus csl_text.c and the test file, no diagnostics, after the
  simplification refactor. This proves parsing only.

Test drivers present but UNRUN, neither compiled nor executed as behavioral tests
(except as noted above for syntax-only parsing where a worker included a test file
in its syntax check):
- tests/csl_emit_smoke.c (34 lines, UNRUN, neither compiled nor executed by emit worker)
- tests/csl_spatial_tests.c (280 lines, UNRUN-marked)
- tests/tsir_csl_tests.c (283 lines, UNRUN driver)

Coordinator verification in this pass (not a test run): file presence and line counts
on disk (78 src files, 9,827 lines, all under 999 lines per file, SPDX MIT in first
three lines of every src file/header), remote head read-back confirming clocksmith/fawn
main is still d25e075341219fc398dedb9286c2669a1b966221, and confirmation that nothing
was pushed or committed. That verification does not prove behavior.

Next test step, before any parity claim: compile and run the three test drivers, then
run the per-file behavioral parity tests defined in parity-test.md (Zig emitter vs C
emitter, line-for-line except documented parsed-value exceptions, plus error-code cases),
then cslc compile, then reference-interpreter / simulator execution.
