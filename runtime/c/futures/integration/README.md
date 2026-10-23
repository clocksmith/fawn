# Experimental retained native futures integration

This is the default-off retained Zig consumer of the accepted independent C
futures owner. It is **not** a Zig-free runtime, a production-default switch,
GPU completion qualification, or a speed claim. The frozen [contract](CONTRACT.md)
and [source seals](CONTRACT.sha256) remain unchanged.

## Build with existing tools

Requirements: native Linux x86-64, GCC/G++, ar, Python 3, the repository-pinned
Zig 0.15.2, and an installed Vulkan loader for retained native linking. No device
is created by these checks. Nothing downloads a compiler or driver.

From the repository root, with absolute paths supplied by the caller:

```sh
sh runtime/c/futures/integration/build_archive.sh "$OUTPUT/archive"
cd runtime/zig
"$ZIG" build dropin test-c-futures-integration -j1 -Doptimize=ReleaseFast \
  -Dexperimental-c-futures-archive="$OUTPUT/archive/libdoe_futures_zig.a" \
  --search-prefix "$VULKAN_LOADER_PREFIX" --cache-dir "$OUTPUT/cache" \
  --global-cache-dir "$OUTPUT/global-cache" --prefix "$OUTPUT/install"
```

Omitting the archive flag selects the unchanged `doe_future_ids.zig`, introduces
no C header/bridge/archive dependency, and preserves existing default tests.
The selected generic native test suites deliberately reject the flag: their
unchanged stack fixtures and direct original-owner imports are not a selected C
suite. Use `test-c-futures-integration` for the explicitly adapted integration
cases. Selected non-native targets, the browser-Wasm artifact, and absent or
relative archives are rejected instead of compiling C through Zig or falling back.
The main, compute and full native module closures receive matching archive/header
wiring; only the headless shared artifact is exercised in this increment.

For fresh source-only reproduction of strict C/C++ consumers, selected/default
native artifacts, focused tests, original-owner regressions, and real pthread
setup/cleanup fault subprocesses:

```sh
sh runtime/c/futures/integration/run_checks.sh \
  "$NEW_OUTPUT_DIRECTORY" "$ZIG" "$VULKAN_LOADER_PREFIX"
```

The script requires a new output directory and records commands, outputs, exit
codes and observed costs. Plan enough disk for compiler caches and both artifacts.
Do not substitute old binary results for a fresh source build. C archive sources
are compiled by GCC only; retained platform stubs still follow the existing Zig
build. No library test or link check establishes physical GPU support.

## Lifetime and failure boundary

- The facade selects one identity domain for all five retained consumers.
- The private header exposes the exact C queue/node types for inline Zig storage.
  Runtime layout verification compares sizes, alignments and every field offset
  with the exact GCC archive. This is not a stable public layout promise.
- Real instance allocation explicitly initializes the queue. Ordinary setup
  failure unwinds and returns NULL. Real final instance release deinitializes
  before freeing storage, preserving existing external-texture guards.
- Every C call that may dispatch snapshots an independent instance lease. A
  callback can finish/deinitialize/free its payload and release the final caller
  reference; the separate lease is released only after the C call returns.
  No wrapper read follows a callback that may free it.
- A stable C trampoline reads the current Zig dispatch at delivery, preserving
  pipeline registration before dispatch assignment. Never-initialized finish is
  a no-op; initialized ownerless nodes still retire and deinitialize.
- WaitAny uses a checked owned typed copy, not struct aliasing. Copy allocation
  failure preserves original flags; zero count keeps native success semantics.
- Exceptional cleanup/invariant statuses terminate with diagnostics. They never
  authorize freeing reserved synchronization storage or fake successful IDs.

## Test surfaces and limitations

The selected Zig cases execute actual native CreateInstance/AddRef/Release/WaitAny/
ProcessEvents and actual `callback_delivery.publish/ready`, including real payload
allocation/destruction and worker publication. Observer state owns no reference
and does not substitute refcount/destruction logic. The C ABI consumer dynamically
loads the selected/default artifacts and calls their actual native instance
exports. It never creates an adapter/device.

The device-loss and pipeline producer imports are selected consistently by the
shared build. Their physical producer/backend execution remains unqualified;
delayed dispatch assignment is exercised at the narrow real adapter seam, not
advertised as GPU pipeline compilation.

Optional `build_archive.sh OUTPUT address,undefined` and `... thread` compile
instrumented C archives. Corresponding native consumers must link matching
sanitizer runtimes. Such instrumentation covers C/bridge consumers only, not
all retained Zig memory or synchronization. LeakSanitizer availability is separate
from AddressSanitizer. See [implementation provenance](PROVENANCE.md) for this
candidate's observed results, first failures and boundaries.
