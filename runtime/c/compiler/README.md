# Experimental standalone C compiler

The current M13 implementation adds all-entry compute modules, explicit NONE/AUTO/NAME
selection and source-static resource/workgroup interfaces to the
[accepted M12 owner](MILESTONE_12_RESULTS.md). Focused developer checks are passing;
independently sealed integrated qualification remains pending. Production defaults
remain Zig. The [frozen M13 contract](MILESTONE_13.md) and
[semantic-Walk liveness amendment](MILESTONE_13_AMENDMENT_01.md) describe this boundary.

M12 extends one shared WGSL compiler
with typed scalar/vector numeric builtin overloads, preserving workgroup storage,
uniformity, aggregates, calls, checked layout and caller-owned reflection. The four retained
public headers remain unchanged; ABI5 is additive. This is an isolated
[C migration](../../../docs/c-migration/MIGRATION.md), not complete WGSL,
production replacement, native runtime adoption or physical GPU qualification.

The additive [texture/sampler family](TEXTURES.md) and [vertex/fragment interfaces](STAGES.md)
now have focused software checks. ABI7 uses explicit interface pools; unchanged
compute layouts remain available. Native graphics orientation, pipeline admission
and physical execution are not inherited from SPIR-V validation.

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

The focused developer command is `make -C runtime/c/compiler test-module`.
Its C/C++ ABI checks, all-entry SPIR-V validation and selected-body instruction
execution are not the independently sealed acceptance suite. It includes the
semantic-Walk overlap regression and does not run the expensive retained witness.

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

The shared library exports exactly thirteen symbols: the five retained compile,
workspace and job entrypoints, plus `doe_compiler_typed_requirements` and
`doe_compiler_compile_typed`, plus the two additive
[ABI5 module operations](include/doe_compiler_module.h), and the two additive
[ABI6 resource operations](include/doe_compiler_resources.h), and the two additive
[ABI7 stage operations](include/doe_compiler_stages.h). See [ABI4](include/doe_compiler_typed.h), the
[frozen numeric semantics](MILESTONE_12.md) and [lowering ledger](MILESTONE_12_LEDGER.md),
with retained storage ownership.

- ABI1, v2 and job retain their layouts and real capacity/projection limits.
  New valid syntax shares the same parser and semantic implementation; old
  rejection alone is not a permanent language dialect.
- ABI5 adds sparse entry/input/reference pools, every declared workgroup root,
  owned per-entry names, LocalSize and builtin masks. Each entry has its own
  transitive source-static resource/workgroup closure. All successful selectors
  emit the identical full module; AUTO requires exactly one entry. Dead/skipped
  source and unused siblings still undergo whole-module validation.
- Duplicate binding pairs are admitted only in disjoint entry interfaces.
  Each emitted entry initializes only its own static workgroup roots before
  its first user operation. Declared roots remain reflected even when unused.
- ABI4 WORDS admits multiple entries; its singular REFLECTION projection rejects
  them after whole-module validation. Old ABI1/v2/job reject at a second entry
  rather than selecting the first. Their public layouts are unchanged.
- ABI4 sizes explicit logical pools through a versioned query. The caller owns
  suitably aligned workspace and disjoint source/result/output spans, one active
  call per workspace. The library allocates nothing and retains no caller pointers.
- WORDS, REFLECTION and MEASURE selections all run validation and private target
  emission with the same configured capacities. Measure is not a parser shortcut.
  Output counts/pool diagnostics are explicit; failures publish no partial words
  or reflection. Caller floating environment and errno are restored before success.
- Reflection includes all declared nominal structs and resource type closure,
  checked sizes/alignments/member offsets/strides and runtime-tail metadata. It is not production TranslationInfo or cache identity. ABI5 additionally
  publishes per-entry closure indices, not a runtime resource/lifetime plan.
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
ABI5 reports logical per-entry bytes, not physical allocation or runtime obligations.

The admitted overload families now include min/max/clamp/exp/tanh/sqrt and
dot/length/distance/normalize/cross. Constant and runtime stages remain distinct;
[AbstractInt dot interpretation](contracts/m12_numeric_interpretation.md) is explicit.

The subsequent [experimental subgroup extension](SUBGROUPS.md) adds eleven typed
arithmetic/bitwise reduction and scan builtins, four subgroup inputs and separate
subgroup/workgroup uniformity analysis. Its actual SPIR-V words and ABI5 metadata
have focused developer checks; collective execution and device feature admission
remain pending. `make test-subgroups` runs these checks.

The [resource-family increment](TEXTURES.md) adds typed texture/sampler handles,
real guarded image loads/stores/queries and explicit-level sampling. ABI6 publishes
sparse handle metadata without inventing buffer layout for image resources.
`make test-resources` runs its focused C/C++ and target checks.

`arrayLength` accepts the address of a storage runtime array or its direct final
structure member. It uses resolved resource identity and the existing runtime-tail
layout; SPIR-V shares one `OpArrayLength` owner with robust buffer accesses. Its
result is uniform runtime metadata, not mutable buffer contents. Skipped/dead
calls still contribute source-static resource closure and validation. Local pointer
aliases and pointer formals remain explicitly unsupported; this is not a claim of
general pointer support. No ABI, workspace record or capacity query changes.
`make test-array-length` checks actual words, closure, short-output atomicity,
reuse, and the unchanged retained timestamp-normalization kernel with specialization
constants. Software Vulkan execution is a separate runtime receipt, not physical
GPU qualification. Rules follow WGSL CRD2026-10-21 section17.4.1.

The ABI8 matrix extension uses the existing stage front door and unchanged
reflection records. Pass `DOE_COMPILER_MATRIX_ABI_VERSION` (8) explicitly;
`DOE_COMPILER_LATEST_ABI_VERSION` exposes the current capability. Matrix reflection
kind6 names a column-vector element type, column count, row width and natural
column stride. Older selected reflection rejects only matrix-reachable output
graphs; binary-only compilation and non-reflected local matrices share the same
pipeline. No new exports, private records or workspace query growth are required.

All nine f32 matrix shapes support typed/inferred constructors, explicitly typed
zero values, copies,
column indexing, aggregate storage, helper parameters/results, addition/subtraction,
scalar/vector/matrix multiplication, transpose and square determinant. AbstractFloat
constant matrices retain the compact constant DAG and checked numeric owner.
Uniform two-row matrices retain their natural 8-byte column stride in reflection.
Their affected SPIR-V uniform resources use an exact-byte uvec4 prefix and optional
scalar/vector tail, with guarded scalar-lane reads and logical reconstruction. This
physical target view neither repacks source bytes nor requires an unenabled Vulkan
uniform-layout feature. Its traversal/constants/words use the ordinary V/D/W pools;
each additional target ID is charged to a serialized defining instruction within W.
Physical uniform byte-offset arithmetic currently has an explicit u32 format limit.
The other matrix layouts retain ordinary SPIR-V matrix types and column decorations.
`make test-matrices` exercises source semantics, actual Vulkan1.1 word validation,
exact constant results, ABI8 graph publication, old projection failure and reuse.
Software/physical execution and complete retained-source coverage remain separate.
Rules follow [WGSL CRD2026-10-21](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
sections8.8 and17.1.2, and the Khronos GLSL.std.450 determinant definition.

All-module APIs accept valid empty or entryless library modules with NONE
selection. They still validate every declaration, helper body and dependency,
then publish zero SPIR-V words plus declared metadata. No synthetic shader entry
or Vulkan Linkage artifact is manufactured. AUTO/NAME selection still requires
a matching shader entry, and retained singular APIs still require one entry.
An empty source span may be NULL with length zero; NULL with positive length
remains an input error. `make test-library-modules` checks these boundaries.

Other barriers, atomics, general pointers, aliases,
overrides, f16, remaining subgroup operations, remaining texture operations, wider builtins,
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

Current measured fixed workspace is 146,048 bytes; public alignment queries remain
authoritative for allocation. The retained M12 conservative bounds remain historical, not proof of changed code.
M13 developer normal/PIC .su/.ci audits enumerate every frame and recursive SCC,
verify GCC local aliases with nm, and bound complete acyclic call paths separately
from recursive families and wrappers. The older sum of every unrelated function
now exceeds its historical coarse subtotal, so it is retained as a failed bound,
not silently claimed to pass. Full source-bound integrated review remains pending.
These are derived bounds, not observed stack peaks; they exclude
external libc/libm, OS and sanitizer frames. Caller-controlled capacities do not
increase bounded expression/control/type recursion. The frozen ledger owns exact
pool accounting and ceilings; future growth requires an explicit reviewed change.

- lex.c / parse.c: one grammar and original source positions.
- numeric.c: checked scalar constants/conversions and per-call host environment.
- builtin.c / sema_builtin.c: shared overload identity, typed snapshots and builtin constants.
- image.c / sema_image.c / sema_image_call.c: typed handle resources and image overloads.
- spirv_image.c: checked real image queries, access and sampling.
- sema_finalize.c: nonrecursive typed numeric/swizzle value finalization.
- sema_array.c: direct runtime-array address and length validation.
- sema_matrix.c / spirv_matrix.c: matrix overloads, constants and target operations.
- spirv_uniform.c: exact-byte physical views for baseline uniform matrix layout.
- types.c / constants.c: shared typed layout and compact aggregate constants.
- sema_declarations.c / sema_expression.c / sema_call.c / validate.c: module and
  lexical identity, typed values/references/calls and structured control.
- uniformity.c: bounded source dependency/control analysis before target emission.
- storage.c / internal.h: checked caller slices, records and fixed adapters.
- module.c: resolved-source sparse closures, iterative graph work and binding collision checks.
- spirv_module.c / spirv_decl.h: typed target planning and module declarations.
- spirv.c / spirv_internal.h: function emission, safe operations and robust lowering.
- reflection.c: typed graph publication and explicit legacy projections.
- compile.c: preflight, diagnostics, environment ownership and atomic publication.
- cli.c / cli_json.c: caller allocations, old JSON and owned file publication.

[Style](STYLE.md), [provenance](PROVENANCE.md), immutable milestone results and
[remaining-owner roadmap](../../../docs/c-migration/COMPILER_ROADMAP.md) preserve
historical evidence and the full compiler migration gates.

The final full-feature50% source-reduction goal includes the complete delivery
burden. No such reduction or complete compiler migration is claimed by M13.
