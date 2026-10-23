# Experimental C native-instance owner

This experimentally selected owner passes independent Linux x86-64 ReleaseFast
CPU/host acceptance against the [frozen ownership contract](CONTRACT.md) and
[opaque C header](include/doe_instance.h). See [results](RESULTS.md) and the
[source-bound independent report](independent_tests/FINAL_REPORT.md).
Debug, GPU/adoption and LeakSanitizer remain unqualified. Native sanitizer
coverage is limited to the seven named C units, not retained Zig or system code.
One C allocation owns the sole instance count and accepted future queue. Copied
queue views borrow; callbacks, requests and enclosing C calls retain their real
leases. There is no Zig instance shell or second identity/queue/count domain.

## Build the C owner

Use existing GCC/G++ and the pinned repository WebGPU header. For an absolute
private output directory:

```sh
sh runtime/c/futures/integration/build_archive.sh "$OUTPUT/futures"
sh runtime/c/instance/integration/build_archive.sh "$OUTPUT/instance"
make -C runtime/c/instance check BUILD="$OUTPUT/check" \
  FUTURES_ARCHIVE="$OUTPUT/futures/libdoe_futures_zig.a"
```

The instance archive contains only instance/wait/capability objects. It invokes
no Zig compiler and links one separately GCC-built accepted future archive.
C++ consumers use the same opaque C boundary, without exceptions or RTTI.
The private fault-test build reduces only the test reference maximum; production
remains UINT32_MAX and exposes no reference-count mutation API.

## Select the mixed-language native consumer

```sh
sh runtime/c/callbacks/integration/build_archive.sh "$OUTPUT/callbacks"
cd runtime/zig
DOE_BACKEND=metal "$ZIG" build dropin test-c-instance-integration -j1 \
  -Doptimize=ReleaseFast \
  -Dexperimental-c-futures-archive="$OUTPUT/futures/libdoe_futures_zig.a" \
  -Dexperimental-c-callbacks-archive="$OUTPUT/callbacks/libdoe_callbacks.a" \
  -Dexperimental-c-instance-archive="$OUTPUT/instance/libdoe_instance.a" \
  --search-prefix "$EXISTING_LOADER_PREFIX" --cache-dir "$OUTPUT/cache" \
  --global-cache-dir "$OUTPUT/global" --prefix "$OUTPUT/selected"
```

Native consumers remain Zig and platform bridges: this is honestly a
mixed-language artifact. All three archive paths must be absolute, existing and
explicit; the selection requires native Linux x86-64. Omit the instance flag for
retained instance plus C futures/callbacks; omit callbacks too for futures-only;
omit all flags for unchanged default owners. Incompatible old selected fixtures
reject explicitly under the opaque instance flag and remain separately runnable.
The generated instance suite adapts all eleven future and five callback
obligations, then adds actual copied-view, loss-event and unconfigured-surface
ownership/failure checks. It is not a blanket retained-suite pass.

[Instrumentation](INSTRUMENTATION.md) restricts host allocation observation to
non-owning, test-only bindings. [Author requirements](EVALUATION.md) are not the
independent evaluator's cases or verdict. Preserve first failures and source
identities. The inherited canonical DSO consumer configuration is ReleaseFast;
Debug remains unqualified after matched default/selected getProcAddress crashes;
ReleaseFast success is not a Debug pass or a diagnosis of that failure.

CPU Metal-unavailable requests and real unconfigured Linux surfaces do not
qualify adapter/device/pipeline success, imported textures, configured surfaces,
actual GPU loss/unknown completion, physical backends or package/browser adoption.
Default production selection is unchanged. See [provenance](PROVENANCE.md),
[style](STYLE.md), the [native seam](integration/CONTRACT.md), and the
[accepted additive future-view contract](../futures/integration/INSTANCE_VIEW_AMENDMENT.md).

For independent public C/C++ and native reproduction, follow the [portable test
entrypoints](independent_tests/README.md). The original 1,719-input manifest remains immutable. Pass the separately bound
`NATIVE_ACCEPTANCE_INPUTS.sha256` for this accepted checkout: only this README
and CATSCAN navigation differ from the original inputs, as recorded in
`NATIVE_ACCEPTANCE_DELTA.json`. Use fresh private output/cache directories.
