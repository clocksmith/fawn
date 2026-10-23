# Callback developer checks

These focused checks are developer evidence, not independent acceptance.
The same public API/archive is used by C11 and restricted C++17 consumers.

```
make -C runtime/c/futures all
make -C runtime/c/callbacks check
```

The C test checks validation precedence, failed allocation/layout without ID or
resource transfer, over-alignment, allocator-token copy and exact free pairing,
inline payload-copy lifetime, all delivery modes, self-wait while registered,
last-reference destruction after the outer queue call, and actual pthread
publication. Ownerless reentry establishes callback-before-legacy-ID order.

`test_faults.c` uses link wrapping at the accepted queue boundary. It injects
identity exhaustion, registration failure, post-publication/post-retirement
SYSTEM_ERROR, and completion-deinit failure. Each case must abort with its named
operation and status; a normal return is never acceptance. The author receipt
records exact link flags and subprocess results. Fault wrappers do not implement
a second completion queue.

Selected native tests are separately registered through the existing Zig test
inventory. Actual Metal-unavailable RequestAdapter and ownerless native error APIs
are exercised by `integration/native_consumer.c`, not by fake backend objects.
Successful adapter/device/resource or physical completion is not established.
