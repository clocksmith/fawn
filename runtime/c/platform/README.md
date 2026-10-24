# Private C host primitives

[doe_platform.h](include/doe_platform.h) is a header-only leaf used by futures,
labels, error scopes and the native host. The library owners keep their existing
queues, callback rules and leases. No new library, allocation or scheduler is
introduced. Every translation unit uses the same platform selection. Include the
header before system headers, or select POSIX/Darwin feature namespaces in the
build before any include.

POSIX uses genuine pthread storage, monotonic condition attributes on Linux and
Darwin's relative condition wait. Win32 uses SDK SRW locks for ordinary mutexes,
critical sections for recursive mutexes, condition variables, `_beginthreadex`,
actual thread handles and integer QPC conversion. Finite Windows waits round up
to milliseconds and split spans before the INFINITE sentinel; timeout chunks
recheck the monotonic deadline. No wall clock or floating-point conversion is
used. Native errors are POSIX/CRT codes or bit-preserved Win32 DWORD values in
`native_error`; status, rather than a signed error-code test, determines success.

Initialize exclusively at the storage's final address. A failed initializer may
still own native attributes, including a SYSTEM_ERROR result. Destroy that
partial object before freeing its storage. Destroy stops on the first failure,
preserves live parts and may be retried exclusively. Never copy an initialized
primitive. The C11 static plain-mutex initializer is for process/static owners
that already have a destruction boundary; it creates no lazy initialization.

Mutex/condition operations require live objects and owner exclusion against
destruction. Condition waits require the lock held exactly once, including a
recursive mutex. They release and reacquire that same lock. Success may be a
spurious wake: the caller owns its predicate loop and timeout/readiness ordering.
Deadlines are normalized, nonnegative monotonic timespec values. Addition checks
signed time_t limits and leaves output untouched on failure. `_at` accepts the
caller's already observed monotonic time, preserving one bounded wait policy.

Thread create borrows the record, callback and argument. The record stays at its
final address until the trampoline has copied callback/argument: creation return
alone does not prove entry. Keep it alive until entry acknowledgement or join.
The trampoline never accesses the record after invoking the callback, permitting
an owner-controlled self-detach/finalization after entry. Serialize publication
and worker access with the owner's lock; the API does not add a second protocol.
Control join/detach exclusively. Success consumes the stored real native handle;
failure retains it and the cleanup obligation. Failed join is not completion.
Successful detach also does not establish completion: callback argument/state
must remain alive through the worker's last use. Never join while holding a lock
the worker needs.

`make -C runtime/c/platform check` builds and runs the shared C CPU control and a
C++17 header consumer. It exercises signal/ready ordering, recursive depth1 waits,
timeout/overflow, early detach with entry acknowledgement and actual join/handle
consumption. Windows additionally probes real invalid-handle errors with a
retained duplicate for recovery. `check-faults` is a GNU/Linux linker-wrapper
control of recursive attribute/init cleanup and failed join/detach ownership.
The futures, labels and error-scopes owner fault tests remain unchanged.

These controls establish host primitives only. Windows source is not an executed
Windows claim until the actual SDK workflow runs it. Historical owner archive
contracts, sealed evidence and production Zig selection are unchanged.
