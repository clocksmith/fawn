# Independent callback ownership evaluation sources

These exact test sources and oracles were authored from the frozen public
callback/futures contract and retained baseline sources, before callback
candidate source inspection or calls. Requirements were sealed independently
first. A pre-fixture-seal declaration-list grep exposed only the already frozen
borrowedQueue signature in the changed future adapter, not its body; this narrow
exposure is disclosed, so this is not represented as fully blind to declarations.

callback_api.c uses public C headers and real accepted queue, typed malloc storage,
real atomic owner leases and actual final queue destruction. Link wrappers record
retirement order and inject terminal status failures, never replace queue policy.
callback_cpp.cpp uses legal C++17 placement construction of concrete Context.
native_consumer.c loads actual exports and requires exact Metal-unavailable and
ownerless diagnostics; all invocations begin in fresh processes. native_cases.zig
uses actual native instance creation/destruction, selected generic owner and real
CPU task pool. It creates no fake adapter/device/backend/GPU objects.

run_public.py --repo CHECKOUT --out NEW_OUTPUT builds GCC owner archives from
source, then C/C++ tests and isolated fatal cases. --callback-archive and
--futures-archive select explicit prebuilt inputs for separate ABI reuse checks.
--mode asan or tsan instruments both C owner sources and independent C tests when
building from source. Retained Zig code is not instrumented by those flags.

run_native.py requires the current source manifest, existing Zig 0.15.2, existing
loader prefix and independent GCC archives. It verifies/copies source inputs
before using the independent retained Zig root. --build-shared invokes the real
canonical dropin build from copied source in separate caches, not an author's
prebuilt artifact. --native-library explicitly consumes a prebuilt library and
is not a substitute for --build-shared acceptance. Build source manifest hashes
are captured after candidate publication; they are artifact identity, not mutable
expected outputs. All exact test source/oracle bytes are sealed before inspection.

All outputs require new directories. First failures are retained. A repair has a
new seal, rationale and recheck; it may not weaken requirements or expected values.
No binaries, toolchains, copied production sources, private caches or physical GPU
claims belong in this portable package. Actual physical successes, backend work,
resource-specific successful map/pipeline/device/loss behavior remain separate.
