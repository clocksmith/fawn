# Experimental standalone C compiler

This is the experimental compiler of the isolated [C migration](../../../docs/c-migration/MIGRATION.md), not a production compiler replacement. The frozen [first milestone](MILESTONE_1.md), [structured neighbor-sum extension](MILESTONE_2.md), [scalar-expression/local extension](MILESTONE_4.md) and [flat uniform/workspace extension](MILESTONE_5.md) own the exact admitted language. The [existing emitter-consumer integration](MILESTONE_3.md) owns optional CLI reflection; the [versioned header](include/doe_compiler.h) owns the unchanged ABI. Production Zig and its consumers are unchanged.

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

`build/doe-compiler-c` and `build/libdoe_compiler.so` are built directly from C11 sources with GCC and strict warnings. There is no Zig build, generated Zig artifact, fallback, compiler subprocess, or dependency in the library. The shared library exports only `doe_compiler_compile`, `doe_compiler_compile_v2` and `doe_compiler_workspace_requirements`; the Linux linker script localizes internal cross-file functions against external symbol interposition. The CLI uses POSIX file operations; this increment is exercised on Linux x86-64, not qualified for other host platforms.

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

The ABI1 library wrapper owns a private typed stack workspace. ABI2 queries the
current library's required size/alignment, then uses caller-owned malloc or aligned
allocation storage. The library never allocates or frees, retains pointers, or
requires initialization/reset/destruction. Source, words, result and workspace
spans are mutually disjoint; one active call uses each workspace, while separate
workspaces can run concurrently. Requirements are not a serialized or stable
private layout. ABI/version/exact result-size checks happen before any workspace
access and preserve all caller output on mismatch. Invalid workspace size,
alignment or NULL returns INPUT_ERROR at offset zero without workspace access.
Other admitted failures preserve words and zero result count/reflection. A
workspace is reusable after every result, including failure.

Measured on GCC 14.2.0, Linux x86-64, `-O2`: workspace 96,208 bytes, alignment 8;
Syntax 47,024; token phase 49,184; typed IR 22,880; target scratch 16,392. Syntax
lives throughout the job. Tokens share a typed union with the combined IR/target
phase because no token pointer survives parsing. The ABI1 static frame is 96,272
bytes, down from M4's 105,328, with no heap substitution. The shared core frame is
80 bytes and the v2 wrapper is 64. Expression/primary parser frames are each 912
bytes, at most 16 valid expression levels plus one rejecting level; parentheses
retain a primary frame around recursive expression parsing. Expression lowering
is 176 bytes per level (at most 16), structured parsing/lowering/emission is
80/368/160 bytes with four nested if levels plus entry, and validation/emission
entry frames are 1,552/1,984 bytes. Lowering frames are bounded dynamic frames
including call arguments. These are individual frames, not combined observed
stack high-water marks; CLI input/output ownership is separate.

ABI1 remains unchanged: 88-byte binding, 784-byte reflection, 216-byte diagnostic,
1,008-byte result and 4-byte enums. V2 has 80-byte members, 716-byte structs,
108-byte bindings, 6,676-byte reflection, 6,904-byte result and a 16-byte workspace
query result. Native C/C++ tests check field offsets as well as sizes. Other hosts
must recheck their layout and stack receipts.

## Uniform and reflection-v2 use

Binary-only mode uses v2 without allocating, checking or publishing JSON. The
legacy `--reflection JSON` mode retains exact old JSON bytes and storage-only
ABI1 admission; even an unused new struct rejects without output. Explicit
`--reflection-v2 JSON` admits uniforms and returns the separate schema_version 2
layout. The two reflection options are mutually exclusive; each must be nonempty
and appear at most once. All four option tokens are rejected as missing-path
substitutes. Ordinary storage programs emit identical SPIR-V in all three modes.
The CLI explicitly owns and frees queried workspace and v2 result allocations;
JSON is allocated only on request, using a checked 4,096/32,768-byte v1/v2 bound.
Every acquisition failure cleans owned state without publishing output.

```sh
build/doe-compiler-c --shader-path ../../../bench/kernels/package_vector_scale_add.wgsl \
  --out build/uniform.spv --reflection-v2 build/uniform.json
make test-uniform SPIRV_VAL=/path/to/already-installed/spirv-val
make sanitize-uniform
build/native_consumer_v2 ../../../bench/kernels/package_vector_scale_add.wgsl
```

The new grammar is generic flat one-to-eight u32/f32 members, up to eight declared
structs and one-to-eight combined storage/uniform resources. Struct/resource
order and shared types do not affect identity or resolution. The retained single
module u32 constant still appears before all struct/resource declarations; this
increment does not expand its placement. Scalar member loads work in expressions;
u32 members additionally work as index bases and either condition operand.
Indices retain only one literal/module-constant +/- offset; nonmember condition
RHS remains literal/module-constant only. All declared members, including unused
padding-like names, have their real typed offsets. V2 reflection includes all
structs and resources in declaration order, not entrypoint liveness.

Flat scalar offsets are 4*i, scalar size/alignment is four, and natural struct
size is 4*member_count with alignment four. Uniform address-space aggregate
alignment is separately 16; a three-member uniform minimum is 12, not 16.
Storage minimum extent is one four-byte scalar. Uniform buffers must supply at
least their whole reflected minimum before invocation: short uniforms are a
host/dispatch failure, never compiler zero-fill. Descriptor-offset alignment,
usage/ranges/lifetimes and device limits remain runtime preconditions. Storage
bounds remain independent per-access OOB-load-zero/OOB-store-discard checks;
logical zero-element oracle buffers are not physical zero-byte binding claims.

## Owner map

- `lex.c`: ASCII tokenization, bounded tokens, nested comments and original byte positions.
- `numeric.c`: bounded exact decimal rational parsing and checked integer RNE materialization.
- `parse.c`: bounded statements/expression trees, declarations, indices and scalar lets; no source-template recognition.
- `validate.c`: lexical/module scope, resource identity/access/layout, scalar checking and typed value/control-flow IR lowering, including unreachable source.
- `reflection.c`: deterministic ABI1/v2 projection of validated declaration/layout IR.
- `internal.h`: private syntax, typed bindings and IR records. These are not public ABI.
- `spirv.c`: typed declarations and structured SPIR-V lowering from IR only.
- `compile.c`: versioned ABI admission, diagnostics and one output publication point.
- `cli.c`: input/workspace/result ownership, explicit little-endian serialization and no-clobber artifact publication.
- `cli_json.c`: checked legacy/v2 JSON serialization, separate from file ownership.

The IR carries invocation-x, typed fixed-index uniform loads, robust storage loads/stores, typed constants, scalar addition/subtraction/multiplication/max, unsigned comparisons, structured if/else boundaries and return. Immutable scoped locals retain typed value IDs evaluated once at declaration; they add no host-side interpretation or mutable target state. Its bounded value IDs do not retain source spellings. Index arithmetic wraps modulo 2^32 before the resource-specific guard. Statements execute in source order, and an early return is independent of whether a preceding store was in bounds. Only live arms branch to a merge; a both-arms-return merge has `OpUnreachable`. Untaken arms never form resource pointers or perform resource side effects. Every load branches on its own binding's runtime-array length, forms its element pointer only in the guarded block, and merges the loaded value with a typed zero using `OpPhi`. The store independently checks the destination's own length. No combined minimum-length guard skips valid output elements when an input is short or empty. Separate declarations remain separate descriptor variables, including unused declared resources reflected in declaration order.

## Verification boundary

Developer tests explicitly classify the milestone-1 grammar rejections now admitted by milestone 2: single load/literal RHS, constant-only addition, literal or constant indices, one u32 index offset, multiple statements, return, and structured if/else. Milestone 4 additionally lifts scalar subtraction and branch-local scalar-let rejections, with positive emitted-instruction checks. Type/access and still-invalid scope failures remain. See [developer test coverage](tests/README.md).

Developer tests exercise API/version/size boundaries, deterministic independent calls, retained u32 sources and named constants, unchanged residual source fixtures, invalid syntax/scope/access/types/limits, CLI failure preservation, and generated target modules. Sanitizers use `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer is not available in this execution environment. The CLI itself and injected write/close/link/temporary failures are instrumented by `make sanitize-cli`. Test binaries, temporary files and logs are confined to ignored `build/` through the Makefile; independently invoked checks can use a caller-selected temporary directory.

The external milestone evaluator is separate from these developer tests. Its instruction-level CPU execution and SPIR-V validation are not physical GPU, WebGPU conformance, backend, numerical hardware, application, speed or full-migration qualification. Source admission here does not replace the existing MSL benchmark compiler or qualify a source-to-GPU application consumer. Compiler ownership stays incomplete until broader source coverage, targets and real consumer gates pass. See [provenance](PROVENANCE.md) and [local style](STYLE.md).

The [first increment results](MILESTONE_1_RESULTS.md) and [second increment results](MILESTONE_2_RESULTS.md) bind their accepted scopes and artifact hashes. The [retained M2 independent suite](independent_tests_m2/README.md) covers the unchanged library semantics and reflection-enabled path. The [third increment results](MILESTONE_3_RESULTS.md) and [M3 independent suite](independent_tests_m3/README.md) bind the binary-only consumer and failure checks; no historical rejection result is rewritten. The [historical M1 suite](independent_tests/README.md) remains unchanged for its preserved M1 binary; its intentionally lifted grammar rejections are not current M2 requirements.

## Existing emitter consumer

The unchanged `bench/gates/spirv_val_gate.py:emit_spirv_for_wgsl` invokes a
selected executable with only `--shader-path INPUT --out OUTPUT`. Optional
reflection admits this exact protocol without changing the consumer or the
compiler library. `make test-cli-modes SPIRV_VAL=/path/to/spirv-val` imports
that function and exercises the two real residual sources, neighbor-sum, unchanged scale and ReLU
fixtures, binary/reflection equality, target validation and file ownership.
It does not run full discovery.

The full gate labels emitter failures `EMIT-SKIP`, validates unrelated prebuilt
modules and maps duplicate source stems onto the same output name. Its successful
exit is not all-source compiler acceptance. Its bare validator invocation also
does not establish the explicit Vulkan 1.1 validation used by the focused tests.
Keep emitted coverage, prebuilt validation and unsupported sources separate.
This component integration is not MSL benchmark, runtime or application adoption.

The [fourth increment results](MILESTONE_4_RESULTS.md) and [M4 independent suite](independent_tests_m4/README.md) bind the current expression/local coverage, distinct build artifacts and remaining migration limits.

The [fifth increment results](MILESTONE_5_RESULTS.md) and [M5 independent suite](independent_tests_m5/README.md) bind the accepted uniform/workspace coverage, exact artifacts, phase-separated corpus ledger and remaining migration gates.
