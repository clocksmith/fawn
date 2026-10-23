# Experimental C callback payload owner

The [frozen API](include/doe_callbacks.h) and [ownership contract](CONTRACT.md)
move the actual allocation/publication/retirement responsibility from retained
`doe_callback_delivery.zig`, not its per-API typed reply semantics. Independent
[acceptance](RESULTS.md) passes for the explicit CPU/headless scope.
The [exact selected seam](integration/CONTRACT.md) preserves the original owner
and tests and requires the already accepted C futures integration.

## Standalone build

```
make -C runtime/c/futures all
make -C runtime/c/callbacks check
```

Existing GCC builds `build/libdoe_callbacks.a`; link it before one accepted
`libdoe_futures.a` and pthreads. The archive contains only the callback owner, not
another queue or ID domain. Public headers are C11/C++17 compatible. See
[developer checks](tests/README.md). The C path never invokes or links Zig.

## Selected retained native build

Use existing tools and absolute caller-owned output paths:

```
sh runtime/c/futures/integration/build_archive.sh "$OUTPUT/futures"
sh runtime/c/callbacks/integration/build_archive.sh "$OUTPUT/callbacks"
cd runtime/zig
DOE_BACKEND=metal "$ZIG" build dropin test-c-futures-integration test-c-callbacks-integration -j1 \
  -Doptimize=ReleaseFast \
  -Dexperimental-c-futures-archive="$OUTPUT/futures/libdoe_futures_zig.a" \
  -Dexperimental-c-callbacks-archive="$OUTPUT/callbacks/libdoe_callbacks.a" \
  --search-prefix "$EXISTING_LOADER_PREFIX" --cache-dir "$OUTPUT/cache" \
  --global-cache-dir "$OUTPUT/global-cache" --prefix "$OUTPUT/selected"
```

The native Linux x86-64 artifact uses approved Zig for retained consumers and
existing backend stubs; GCC compiles the C owners. Omit the callback option for
accepted futures-only selection. Omit both for original default Zig with no C
owner/header/archive dependency. Callbacks without C futures, missing/relative
archives and unsupported selected targets reject explicitly.

The real native consumer uses a fresh process and existing DOE_BACKEND=metal.
Linux's preexisting Metal bridge rejects adapter creation with status3, NULL
adapter and `metal default device unavailable`. This exercises real owned reply
allocation/retirement; null-handle API cases are separately ownerless. Neither
proves successful backend/GPU execution. Production/default replacements remain
zero; native adapters are transitional Zig, not an omitted migration obligation.

[Provenance](PROVENANCE.md) preserves the source identities. No performance,
whole-runtime sanitizer, release or complete language-migration claim is made.

## Independent reproduction

The exact [independent report](independent_tests/REPORT.md),
[portable commands](independent_tests/REPRODUCE.md), first-failure explanations and
sealed test sources are retained here. The acceptance reproduction and
`SOURCE_INPUTS_CURRENT.sha256` bind commit
`3582e86472db945cc441eedb3796ec7f1b13d179`, not arbitrary later HEADs. On that
accepted tree, supply
`--source-manifest runtime/c/callbacks/SOURCE_INPUTS_CURRENT.sha256` to the native
runner instead of the historical candidate manifest; [provenance](PROVENANCE.md#acceptance)
records the acceptance-document distinction. Use absolute runner paths and
fresh output directories. The current standalone incremental-link regression
is [tests/test_build_dependencies.py](tests/test_build_dependencies.py);
it does not refresh the historical native acceptance.
