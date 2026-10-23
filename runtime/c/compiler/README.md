# Experimental standalone C compiler

This is the experimental compiler of the isolated [C migration](../../../docs/c-migration/MIGRATION.md), not a production compiler replacement. The frozen [first milestone](MILESTONE_1.md) and [structured neighbor-sum extension](MILESTONE_2.md) own the exact admitted language. The [existing emitter-consumer integration](MILESTONE_3.md) owns optional CLI reflection; the [versioned header](include/doe_compiler.h) owns the unchanged ABI. Production Zig and its consumers are unchanged.

## Build and consume

From this directory, using the installed native toolchain:

```sh
make all
make test SPIRV_VAL=/workspace/shared/doe-language-experiment-20261023/phase2/deps/spirv-tools/usr/bin/spirv-val
make sanitize
make sanitize-cli SPIRV_VAL=/path/to/already-installed/spirv-val
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
  --out build/example.spv

# Request the optional JSON explicitly, using fresh destinations.
build/doe-compiler-c \
  --shader-path ../../../bench/inference-pipeline/kernels/residual.wgsl \
  --out build/reflected.spv --reflection build/example.json
```

The source is compiled unchanged. `--shader-path` and `--out` are required exactly once; `--reflection` is optional, at most once. An explicit empty reflection path is an error, not omission. A recognized option token cannot stand in for a missing path; use an unambiguous `./` prefix for a filename equal to `--shader-path`, `--out` or `--reflection`. Other ordinary path names remain allowed. Without reflection the CLI publishes only little-endian SPIR-V, with no default sidecar. Explicit reflection adds the same complete JSON as before and does not change the binary bytes. Existing destinations are refused, including symlinks. When reflection is requested, the first no-clobber publication is rolled back on an ordinary second-publication failure; this is not a crash-atomic multi-file transaction.

A native caller includes `doe_compiler.h` and supplies ABI version 1, source pointer and byte length, a disjoint word buffer and capacity, and a result pointer and exact byte size. Source need not be NUL-terminated. ABI/size mismatch leaves result and words untouched. Other failures zero result count/reflection and provide an original-source diagnostic while preserving the entire output buffer. A NULL result returns an input error. No input or output pointer is retained.

The library uses bounded stack workspace, no heap allocation, and no mutable global state. These are this milestone's properties, not a mandate that every future compiler owner must avoid heap allocation. GCC 14.2.0 at `-O2` reports an 88,672-byte static frame for `doe_compiler_compile`, excluding its callees and caller buffers (milestone 1: 68,736 bytes; increase: 19,936 bytes). Structured parsing, lowering and emission recurse to at most four if levels; GCC reports 64-, 368- and 160-byte static recursive frames respectively. The exercised Linux x86-64 ABI has 4-byte enums, 88-byte binding records, 784-byte reflection, 216-byte diagnostics and a 1,008-byte result. Tests assert field offsets as well as sizes. Stack budget and native ABI layout require rechecking for a different host.

## Owner map

- `lex.c`: ASCII tokenization, bounded tokens, nested comments and original byte positions.
- `parse.c`: bounded preorder statement syntax, declarations, names, indices and operands; no source-template recognition.
- `validate.c`: lexical/module scope, resource identity/access, scalar checking, typed value/control-flow IR lowering and reflection, including unreachable source.
- `internal.h`: private syntax, typed bindings and IR records. These are not public ABI.
- `spirv.c`: typed declarations and structured SPIR-V lowering from IR only.
- `compile.c`: versioned ABI admission, diagnostics and one output publication point.
- `cli.c`: input ownership, explicit little-endian serialization and no-clobber artifact publication.

The IR carries invocation-x, robust resource loads/stores, typed constants and addition, u32 index subtraction, unsigned comparisons, structured if/else boundaries and return. Its bounded value IDs do not retain source spellings. Index arithmetic wraps modulo 2^32 before the resource-specific guard. Statements execute in source order, and an early return is independent of whether a preceding store was in bounds. Only live arms branch to a merge; a both-arms-return merge has `OpUnreachable`. Untaken arms never form resource pointers or perform resource side effects. Every load branches on its own binding's runtime-array length, forms its element pointer only in the guarded block, and merges the loaded value with a typed zero using `OpPhi`. The store independently checks the destination's own length. No combined minimum-length guard skips valid output elements when an input is short or empty. Separate declarations remain separate descriptor variables, including unused declared resources reflected in declaration order.

## Verification boundary

Developer tests explicitly classify the milestone-1 grammar rejections now admitted by milestone 2: single load/literal RHS, constant-only addition, literal or constant indices, one u32 index offset, multiple statements, return, and structured if/else. They do not remove old scope/type/access failures. See [developer test coverage](tests/README.md).

Developer tests exercise API/version/size boundaries, deterministic independent calls, retained u32 sources and named constants, unchanged residual source fixtures, invalid syntax/scope/access/types/limits, CLI failure preservation, and generated target modules. Sanitizers use `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer is not available in this execution environment. The CLI itself and injected write/close/link/temporary failures are instrumented by `make sanitize-cli`. Test binaries, temporary files and logs are confined to ignored `build/` through the Makefile; independently invoked checks can use a caller-selected temporary directory.

The external milestone evaluator is separate from these developer tests. Its instruction-level CPU execution and SPIR-V validation are not physical GPU, WebGPU conformance, backend, numerical hardware, application, speed or full-migration qualification. Source admission here does not replace the existing MSL benchmark compiler or qualify a source-to-GPU application consumer. Compiler ownership stays incomplete until broader source coverage, targets and real consumer gates pass. See [provenance](PROVENANCE.md) and [local style](STYLE.md).

The [first increment results](MILESTONE_1_RESULTS.md) and [second increment results](MILESTONE_2_RESULTS.md) bind their accepted scopes and artifact hashes. The [retained M2 independent suite](independent_tests_m2/README.md) covers the unchanged library semantics and reflection-enabled path. The [third increment results](MILESTONE_3_RESULTS.md) and [M3 independent suite](independent_tests_m3/README.md) bind the binary-only consumer and failure checks; no historical rejection result is rewritten. The [historical M1 suite](independent_tests/README.md) remains unchanged for its preserved M1 binary; its intentionally lifted grammar rejections are not current M2 requirements.

## Existing emitter consumer

The unchanged `bench/gates/spirv_val_gate.py:emit_spirv_for_wgsl` invokes a
selected executable with only `--shader-path INPUT --out OUTPUT`. Optional
reflection admits this exact protocol without changing the consumer or the
compiler library. `make test-cli-modes SPIRV_VAL=/path/to/spirv-val` imports
that function and exercises the two real residual sources and neighbor-sum
fixture, binary/reflection equality, target validation and file ownership.
It does not run full discovery.

The full gate labels emitter failures `EMIT-SKIP`, validates unrelated prebuilt
modules and maps duplicate source stems onto the same output name. Its successful
exit is not all-source compiler acceptance. Its bare validator invocation also
does not establish the explicit Vulkan 1.1 validation used by the focused tests.
Keep emitted coverage, prebuilt validation and unsupported sources separate.
This component integration is not MSL benchmark, runtime or application adoption.
