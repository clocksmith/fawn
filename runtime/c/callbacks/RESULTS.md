# Callback payload owner acceptance

Component: experimental callback allocation/publication/retirement owner.
Intent preserved. Independent CPU/headless acceptance passes; production-default
replacement count remains zero. The exact [independent report](independent_tests/REPORT.md)
is authoritative, with [portable reproduction](independent_tests/REPRODUCE.md).

The GCC-only C owner passes source and prebuilt C/C++ consumers, ownership and
allocation-fault checks, callback reentry, final-reference destruction, ASan/UBSan
and C-only TSan. The selected retained native consumers pass nine independently
authored cases plus actual exported C/C++ RequestAdapter error-path checks.
Fresh independent selected and futures-only canonical shared libraries pass;
the current author-built default library is separately independently consumed.
Original owner tests and the accepted queue regression remain intact.

The first native candidate mishandled a stateless allocator's unused pointer.
Candidate2 fixes its reconstruction without changing the C core; a separately
sealed deferred-allocator regression passes. Original failures and the exact
blindness boundary are preserved in the report, not erased by final acceptance.

Real native evidence exercises the existing Linux Metal-unavailable response,
not adapter success or GPU execution. Typed API deliverers, successful resource
and device paths, backend parity, physical GPU completion, other platforms,
performance and release qualification remain open. C instrumentation does not
qualify retained Zig allocations or reference counts. LeakSanitizer is unqualified
because this executor rejected it under ptrace.

Only callback/futures selection and ownership transfer changed at the native
boundary. Default selection still uses retained Zig without C-owner linkage;
selected C callbacks require the accepted C queue, never a second queue domain.
See [provenance](PROVENANCE.md#acceptance) for exact source-manifest handling.
