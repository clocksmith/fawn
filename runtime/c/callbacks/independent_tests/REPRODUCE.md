# Independent callback owner reproduction

This package contains only independent test/driver/document sources and manifests.
It contains no candidate production sources, objects, archives, native binaries,
cache, toolchain, credentials or GPU claims. The MIT license is retained.
Use a checkout matching NATIVE_SOURCE_INPUTS_V2.sha256, Linux x86-64, GCC/G++ 14.2,
Python 3.12, Make and the already-approved Zig 0.15.2 plus existing Vulkan loader
link prefix. Do not install tools to run this package implicitly.

## Verify identities

From this package root, `sha256sum -c FIXTURES_SEAL_V3.sha256` verifies current
fixtures. `sha256sum -c REQUIREMENTS_SEAL.sha256` verifies independent requirements
and the frozen draft-input manifest. Earlier fixture seals and original changed
files are retained for provenance; they do not validate the newer files at their
old paths. See FIXTURE_REPAIRS.md. The native runner verifies the production
source manifest against the supplied checkout before copying it.

## Public C owner, from source and prebuilt

Set absolute shell paths REPO, OUT, ZIG and LOADER to the checkout, a new disposable
output parent, existing Zig executable and existing loader prefix, respectively.
Set PKG to this package root. All runner output leaf directories must be new.

```sh
python3 "$PKG/tests/run_public.py" --repo "$REPO" --out "$OUT/public"
ASAN_OPTIONS=detect_leaks=0 python3 "$PKG/tests/run_public.py" --repo "$REPO" --out "$OUT/asan" --mode asan
python3 "$PKG/tests/run_public.py" --repo "$REPO" --out "$OUT/tsan" --mode tsan
python3 "$PKG/tests/run_public.py" --repo "$REPO" --out "$OUT/prebuilt" \
  --callback-archive /absolute/sealed/libdoe_callbacks.a \
  --futures-archive /absolute/sealed/libdoe_futures_zig.a
```

Record hashes of explicitly supplied prebuilt archives and their source manifest;
prebuilt consumption is separate from the source-build qualification. The C-only
owner archive must be built by GCC, not Zig. The public C++17 copy hook legally
placement-constructs the concrete Context in final untyped storage.

## Native nine-case owner and fresh canonical selected DSO

Build the unchanged accepted C bridge independently and append it to the same
accepted future object set; never add a second queue implementation.

```sh
gcc -std=c11 -O2 -g -fPIC -Wall -Wextra -Wpedantic -Werror -pthread \
  -I "$REPO/runtime/c/futures/include" -I "$REPO/runtime/c/futures/integration" \
  -c "$REPO/runtime/c/futures/integration/doe_futures_zig_bridge.c" \
  -o "$OUT/public/bridge.o"
ar rcs "$OUT/public/libdoe_futures_zig.a" "$OUT/public/futures-futures.o" \
  "$OUT/public/futures-storage.o" "$OUT/public/bridge.o"
python3 "$PKG/tests/run_native.py" --repo "$REPO" --out "$OUT/native" \
  --zig "$ZIG" --loader-prefix "$LOADER" \
  --source-manifest "$PKG/NATIVE_SOURCE_INPUTS_V2.sha256" \
  --callback-archive "$OUT/public/libdoe_callbacks.a" \
  --futures-archive "$OUT/public/libdoe_futures_zig.a" --build-shared
```

This validates nine retained native cases, independently builds the actual
headless dropin DSO from copied/verified source, compiles strict C and C++ public
consumers, and requires eighteen fatal mappings. Assertions remain active under
ReleaseFast. Actual consumer processes set DOE_BACKEND=metal before loading the
DSO and require real owned RequestAdapter status3, null adapter and exact
`metal default device unavailable`. This is not simulated or successful GPU work.

The canonical callbacks+futures build uses both archive flags. To reproduce
futures-only reversibility from the same copied source/runtime/zig directory
or the authoritative checkout runtime/zig directory under source quiescence,
verify all source-manifest entries before and after. Run the following in
separate fresh named caches and installation prefix, then reuse
the native-gcc and native-g++ consumers with that DSO path:

```sh
"$ZIG" build dropin -Doptimize=ReleaseFast \
  -Dexperimental-c-futures-archive="$OUT/public/libdoe_futures_zig.a" \
  --search-prefix "$LOADER" --cache-dir "$OUT/futures-cache" \
  --global-cache-dir "$OUT/futures-global-cache" --prefix "$OUT/futures-install"
"$OUT/native/native-gcc" "$OUT/futures-install/lib/libwebgpu_doe.so"
"$OUT/native/native-g++" "$OUT/futures-install/lib/libwebgpu_doe.so"
```

Default reversibility omits both archive flags with new caches/prefix. Preserve
generated options and inspect exports: default has zero callback/future C API
symbols; futures-only has zero callback and fourteen future; selected has three
callback and fourteen future. Headless execution does not qualify compute/full
artifacts whose matching option wiring was source-audited only.

## Sanitizer limits and first failures

In this executor LeakSanitizer explicitly refused ptrace; initial failure is
retained. detect_leaks=0 is a separately labelled ASan/UBSan recheck, not a leak
qualification. Public TSan C/C++ tests and accepted queue suite passed here.
A different TSan runtime failure must remain a limitation, never be called a pass.

For native C-only ASan/UBSan evidence, build the bridge with the same flags used
by run_public --mode asan, use the instrumented archives and run_native --mode
asan without --build-shared. GCC instruments C owner/futures/bridge, NOT retained
Zig. The first native executable launch here failed because libasan was not first
in the library list. Preserve that receipt, then run the emitted native binary:

```sh
ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD="$(gcc -print-file-name=libasan.so)" \
  "$OUT/native-asan/independent_callbacks"
```

All nine native cases passed with that explicit runtime-order correction. No
native Zig TSan claim is made. Physical resource success, device loss, real GPU
QueueWorkDone completion, backend equivalence, performance, other targets and
release qualification remain unrun boundaries.

Do not overwrite failed receipts. Preserve source/test/options and retained
binaries before deleting only confirmed-completed disposable caches; record full
inventories, generated source and hashes. Serialize native builds on small disks.

## Observed final output-path control

For the final current futures-only check, /tmp was a separate existing tmpfs with
adequate capacity. A private mktemp directory held all new cache/install outputs;
no source snapshot removal or new mount was needed. The primary checkout was
read-only for this action and all 1294 source entries matched before and after.
This does not change candidate identity, options or source-build qualification.
The exact successful command and output identities are in REPORT.md and original
current-futures-only-native receipts. No cached native binary was reused.
