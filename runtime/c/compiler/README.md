# Experimental standalone C compiler

The [accepted M12 owner](MILESTONE_12_RESULTS.md) extends one shared WGSL compiler
with typed scalar/vector numeric builtin overloads, preserving workgroup storage,
uniformity, aggregates, calls, checked layout and caller-owned reflection. Four
public headers remain unchanged. This is an isolated
[C migration](../../../docs/c-migration/MIGRATION.md), not complete WGSL,
production replacement, native runtime adoption or physical GPU qualification.

## Build and decisive checks

From the repository root, ordinary source consumption is:

```sh
make -C runtime/c/compiler all
runtime/c/compiler/build/doe-compiler-c --shader-path input.wgsl --out fresh.spv
```

The C11 library and CLI use GCC, strict warnings, `-fno-fast-math`,
`-ffp-contract=off`, `-frounding-math`, and libm. They invoke no Zig, compiler
subprocess or fallback. Qualified native ABI: Linux x86-64, GCC/G++ 14.2,
C11/C++17. Other platforms/toolchains need their own qualification.

The current independent [source-only gate](independent_tests_m12/PORTABLE_USAGE.md)
provides exact build, semantic, corpus and native commands using an existing
validator and accepted source history. Its retained M11 shared-memory witness is expensive:
the recorded eight-dispatch case took about 85 minutes. M12 carries its exact
target proof forward; a shorter run is not equivalent acceptance. The retained [M10 instructions](independent_tests_m10/REPRODUCE_FINAL.md)
remain historical, not a command to apply obsolete expectations to current source.

Use fresh private output paths. The gate verifies exact source before/after and
builds new artifacts; it does not require a prebuilt compiler. Native C/C++,
sanitizer and host-environment lanes have separate portable instructions beside
that gate. Frozen original tests remain unchanged; the named derived adapters
explain new valid admissions and target-layout changes. Raw historical `make test`
is not a current acceptance gate: it contains obsolete private sizes, export
counts and explicitly lifted negative cases. Never broadly relax those assertions.

## Native API and caller ownership

The shared library exports exactly seven symbols: the five retained compile,
workspace and job entrypoints, plus `doe_compiler_typed_requirements` and
`doe_compiler_compile_typed`. See [ABI4](include/doe_compiler_typed.h), the
[frozen numeric semantics](MILESTONE_12.md) and [lowering ledger](MILESTONE_12_LEDGER.md),
with retained storage ownership.

- ABI1, v2 and job retain their layouts and real capacity/projection limits.
  New valid syntax shares the same parser and semantic implementation; old
  rejection alone is not a permanent language dialect.
- ABI4 sizes explicit logical pools through a versioned query. The caller owns
  suitably aligned workspace and disjoint source/result/output spans, one active
  call per workspace. The library allocates nothing and retains no caller pointers.
- WORDS, REFLECTION and MEASURE selections all run validation and private target
  emission with the same configured capacities. Measure is not a parser shortcut.
  Output counts/pool diagnostics are explicit; failures publish no partial words
  or reflection. Caller floating environment and errno are restored before success.
- Reflection includes all declared nominal structs and resource type closure,
  checked sizes/alignments/member offsets/strides and runtime-tail metadata. It is
  not production TranslationInfo, reachable-entry resource analysis or cache identity.
- Legacy projections reject unrepresentable aggregate resources by name rather
  than pretending vectors/structs are scalar arrays. Public enum/layout meanings
  are not repurposed.

## Source and target behavior

Accepted prior scalar numerics, constants, lexical scopes, structured control and
module functions remain. M10 adds bool/i32/u32/f32 vectors, fixed arrays, nominal
nested structures, constructors, by-value helper values and chained member/index/
swizzle reads with selected component writes. Constants use compact typed DAGs,
not recursive flattening proportional to array length. Module and lexical lookup
precede predeclared constructors/builtins, including shadowed names.

One checked natural-layout result feeds validation, reflection and target lowering.
A vec3 has size 12/alignment 16; array stride is 16. Uniform constraints validate
layout rather than silently repacking it. Explicit @align/@size are owned values.
Runtime arrays occur only at an admitted storage root or its direct final member.
Uniform minimum binding extents remain host dispatch preconditions, not short-buffer
zero-fill. Each access step guards before pointer formation; out-of-bounds loads
produce typed zero and stores discard independently. An inner OOB load's zero may
still be a valid outer index. Bounds do not establish descriptor overlap/race safety.

Each helper emits actual target calls and fresh local state. Source evaluation
order, local snapshots, termination, short circuit and robust Phi predecessors
remain explicit. All dead/unused source is validated. Host numeric evaluation and
raw instruction execution do not promise universal GPU NaN/subnormal payloads or
bit-identical floating arithmetic.

Fixed typed workgroup roots are zero-initialized before user control flow;
`workgroupBarrier` synchronizes only workgroup memory within one group. Uniformity
tracks validated source dependencies, including constant-skipped RHS syntax,
calls, control flow and loop exits. Workgroup roots are not descriptor bindings;
standalone reflection does not supply runtime workgroup metadata.

The admitted overload families now include min/max/clamp/exp/tanh/sqrt and
dot/length/distance/normalize/cross. Constant and runtime stages remain distinct;
[AbstractInt dot interpretation](contracts/m12_numeric_interpretation.md) is explicit.

Other barriers, atomics, matrices, pointers, aliases,
overrides, f16/subgroups, multiple entries, graphics, textures, wider builtins,
other targets and runtime metadata/adoption remain open owners, not implied coverage.

## CLI and actual consumer

`--shader-path` and `--out` are required once. Binary-only mode uses the typed
pipeline without demanding a legacy reflection projection; the unchanged existing
emitter consumer therefore admits new aggregate sources. Optional `--reflection`
selects storage-only ABI1 metadata; `--reflection-v2` retains the old job/schema.
They are mutually exclusive. No JSON4 option exists.

Unknown/duplicate/missing/empty options fail. Existing destinations and symlinks
are refused. A failed second publication rolls back the first owned publication;
this is ordinary-failure rollback, not crash-atomic multi-file durability.
The [same-source ledger](../../../docs/c-migration/corpus-coverage-m12.json)
separates 67 fresh emitted/validated paths, 48 execution-qualified paths,
19 validation-only emissions, 31 explicit rejections and unrelated prebuilt
observations. None is physical execution or complete adoption.

## Storage and owner map

Measured fixed workspace is 146,256 bytes; public alignment queries remain
authoritative for allocation. Independent conservative library bounds are
205,776 fixed and 194,544 maximum-depth
job bytes. These are derived bounds, not observed stack peaks; they exclude
external libc/libm, OS and sanitizer frames. Caller-controlled capacities do not
increase bounded expression/control/type recursion. The frozen ledger owns exact
pool accounting and ceilings; future growth requires an explicit reviewed change.

- lex.c / parse.c: one grammar and original source positions.
- numeric.c: checked scalar constants/conversions and per-call host environment.
- builtin.c / sema_builtin.c: shared overload identity, typed snapshots and builtin constants.
- types.c / constants.c: shared typed layout and compact aggregate constants.
- sema_declarations.c / sema_expression.c / sema_call.c / validate.c: module and
  lexical identity, typed values/references/calls and structured control.
- uniformity.c: bounded source dependency/control analysis before target emission.
- storage.c / internal.h: checked caller slices, records and fixed adapters.
- spirv.c / spirv_decl.h: typed declarations, safe operations and robust lowering.
- reflection.c: typed graph publication and explicit legacy projections.
- compile.c: preflight, diagnostics, environment ownership and atomic publication.
- cli.c / cli_json.c: caller allocations, old JSON and owned file publication.

[Style](STYLE.md), [provenance](PROVENANCE.md), immutable milestone results and
[remaining-owner roadmap](../../../docs/c-migration/COMPILER_ROADMAP.md) preserve
historical evidence and the full compiler migration gates.
