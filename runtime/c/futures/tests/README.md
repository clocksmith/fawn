# Futures developer tests

Run `make -C runtime/c/futures check` from the repository root. These are developer
regressions, not the independently frozen acceptance suite.

- `test_api.c`: source-owner behaviors plus storage/status and reentry boundaries.
- `test_cpp.cpp`: opaque public API consumed from restricted C++17.
- `test_faults.c`: pthread setup/destruction failure injection with exact live
  resource counters, repeated recovery, rejection while cleanup-only and reuse.
- `test_exhaustion.c`: two instances, two sequence values and two legacy values;
  identical production transitions with private test-only endpoint constants.
- `test_time.c`: wrapped monotonic clock and timed condition wait plus a real
  finite wait. Verifies a fixed deadline and ETIMEDOUT winning a readiness race.
- `test_concurrency.c`: real pthread condition handshakes prove indefinite waiter
  startup and overlapping event pumps; every created thread is joined.
- `characterize_zig.py`: separately copies/hashes the pinned production source
  closure and runs six unchanged original owner tests with explicitly supplied
  Zig. It is not called by the C build and must use a new evidence directory.

To instrument the native tests, select a separate BUILD directory and provide
complete CFLAGS/CXXFLAGS/LDFLAGS containing the desired installed sanitizer. An
ASan run can require ASAN_OPTIONS=detect_leaks=0 under a ptrace execution sandbox;
record that excluded LeakSanitizer check rather than claiming complete sanitizer
coverage. Keep compiler/test logs and first failures outside the source tree.
