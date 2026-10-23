# Native sanitizer link-directory repair

The first native sanitizer invocation exits 1 before compilation because the
runner resolved GCC's libasan.so/libubsan.so symlinks and then used the targets'
directory for -l linkage. That directory contains versioned sonames only. The
repair uses the original GCC-reported library directory, retaining -lasan,
-lubsan/-ltsan and the same exact installed runtime bytes. No flags, source,
instrumented objects or test assertions change. Original failure is preserved;
no sanitizer execution is claimed from that failed lookup.
