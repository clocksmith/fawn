# Experimental standalone C compiler

This is the first compiler increment of the isolated [C migration](../../../docs/c-migration/MIGRATION.md), not a production compiler replacement. The frozen [milestone](MILESTONE_1.md) and [versioned header](include/doe_compiler.h) own the exact admitted language and ABI. Production Zig and its consumers are unchanged.

## Build and consume

From this directory, using the installed native toolchain:

```sh
make all
make test SPIRV_VAL=/workspace/shared/doe-language-experiment-20261023/phase2/deps/spirv-tools/usr/bin/spirv-val
make sanitize
make build/native_consumer build/cpp_consumer build/test_layout
build/native_consumer ../../../bench/inference-pipeline/kernels/residual.wgsl
build/cpp_consumer
build/test_layout
```

`make test` requires an existing SPIR-V validator. `SPIRV_VAL` defaults to `spirv-val` on `PATH`; the example explicitly selects the previously approved local validator used for this increment. On another host, supply the path to its already installed validator. The build does not download or install one.

`build/doe-compiler-c` and `build/libdoe_compiler.so` are built directly from C11 sources with GCC and strict warnings. There is no Zig build, generated Zig artifact, fallback, compiler subprocess, or dependency in the library. The shared library exports only `doe_compiler_compile`; the Linux linker script localizes internal cross-file functions against external symbol interposition. The CLI uses POSIX file operations; this increment is exercised on Linux x86-64, not qualified for other host platforms.

```sh
build/doe-compiler-c \
  --shader-path ../../../bench/inference-pipeline/kernels/residual.wgsl \
  --out build/example.spv --reflection build/example.json
```

The source is compiled unchanged. Existing destinations are refused, including symlinks. The CLI emits little-endian SPIR-V and complete JSON reflection. Two no-clobber file publications are rolled back on an ordinary second-publication failure; they are not a crash-atomic multi-file transaction.

A native caller includes `doe_compiler.h` and supplies ABI version 1, source pointer and byte length, a disjoint word buffer and capacity, and a result pointer and exact byte size. Source need not be NUL-terminated. ABI/size mismatch leaves result and words untouched. Other failures zero result count/reflection and provide an original-source diagnostic while preserving the entire output buffer. A NULL result returns an input error. No input or output pointer is retained.

The library uses bounded stack workspace, no heap allocation, and no mutable global state. These are this milestone's properties, not a mandate that every future compiler owner must avoid heap allocation. GCC 14.2.0 at `-O2` reports a 68,736-byte static frame for `doe_compiler_compile`, excluding its callees and caller buffers. The exercised Linux x86-64 ABI has 4-byte enums, 88-byte binding records, 784-byte reflection, 216-byte diagnostics and a 1,008-byte result. Tests assert field offsets as well as sizes. Stack budget and native ABI layout require rechecking for a different host.

## Owner map

- `lex.c`: ASCII tokenization, bounded tokens, nested comments and original byte positions.
- `parse.c`: bounded syntax records for declarations, names, indices and operands; no source-template recognition.
- `validate.c`: lexical/module scope, resource identity/access, scalar checking, typed IR lowering and reflection.
- `internal.h`: private syntax, typed bindings and IR records. These are not public ABI.
- `spirv.c`: typed declarations and structured SPIR-V lowering from IR only.
- `compile.c`: versioned ABI admission, diagnostics and one output publication point.
- `cli.c`: input ownership, explicit little-endian serialization and no-clobber artifact publication.

The IR carries invocation-x, robust resource loads, scalar constants, typed addition and a robust resource store. Every load branches on its own binding's runtime-array length, forms its element pointer only in the guarded block, and merges the loaded value with a typed zero using `OpPhi`. The store independently checks the destination's own length. No combined minimum-length guard skips valid output elements when an input is short or empty. Separate declarations remain separate descriptor variables, including unused declared resources reflected in declaration order.

## Verification boundary

Developer tests exercise API/version/size boundaries, deterministic independent calls, retained u32 sources and named constants, unchanged residual consumers, invalid syntax/scope/access/types/limits, CLI failure preservation, and generated target modules. Sanitizers use `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer is not available in this execution environment. Test binaries, temporary files and logs are confined to ignored `build/`.

The external milestone evaluator is separate from these developer tests. Its instruction-level CPU execution and SPIR-V validation are not physical GPU, WebGPU conformance, backend, numerical hardware, application, speed or full-migration qualification. Source admission here does not replace the existing MSL benchmark compiler or qualify a source-to-GPU application consumer. Compiler ownership stays incomplete until broader source coverage, targets and real consumer gates pass. See [provenance](PROVENANCE.md) and [local style](STYLE.md).

The [first increment results](MILESTONE_1_RESULTS.md) bind the accepted scope and artifact hashes. The [independent source-only suite](independent_tests/README.md) provides a separate reproducible native/semantic acceptance route.
