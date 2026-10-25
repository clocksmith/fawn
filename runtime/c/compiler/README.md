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

The additive [ABI13 override contract](SPECIALIZATION.md) separates whole-module
validation from selected-entry specialization through the same semantic owner.
Uninitialized overrides remain deferred until a pipeline supplies values; earlier
compiler ABI admission and layouts remain unchanged. Focused host/target controls
do not establish native pipeline adoption or GPU qualification.

Core `select` supports scalar/vector numeric and boolean values, scalar or
matching vector boolean masks, eager ordered arguments, and const/override/runtime
stages through one semantic owner. `make test-select sanitize-select` exercises
all target adapters and transactional failures. The unchanged retained source is
[select_retained.wgsl](tests/select_retained.wgsl), extracted from the retained
Zig backend translation test; [select_cases.wgsl](tests/select_cases.wgsl) adds
asymmetric masks, exact value bits and observable argument ordering. Public native
CPU execution is a separate consumer gate; target compilation alone is not that
execution evidence. No parser, public ABI, work limits or production selector is
changed by this builtin addition.

Explicit depth sampling now includes `textureSampleLevel` and
`textureSampleCompareLevel` for depth 2D/array/cube/cube-array handles, including
constant offsets where WGSL permits them. The [texture contract](TEXTURES.md#explicit-depth-sampling)
records exact overloads, comparison direction, target mapping and remaining scope.
`make depth-sampling-check` checks all public target adapters; target SDK validation
and public CPU execution remain distinct evidence.

Implicit `textureSample` and `textureSampleCompare` now use the same image owner
with transitive fragment-only and default derivative-uniformity validation.
`make implicit-sampling-check` covers public target admission, helper/control-flow
restrictions and preserved explicit-level behavior. The [texture contract](TEXTURES.md#implicit-fragment-sampling)
records exact scope and the independent fragment pixel fixture; actual execution
and target validation remain separate evidence.

Core `all` and `any` now reduce bool scalar/vector inputs through the shared
builtin registry, constant DAG and unary IR path. [Boolean reductions](LOGICAL_BUILTINS.md)
records exact admission, eager operand semantics and the unchanged retained and
literal-oracle fixtures. `make all-any-check sanitize-all-any` checks the public
target adapters and failure/state boundaries; actual execution remains separate.

`fma`, `step`, `smoothstep` and `saturate` support abstract-float and f32
scalar/vector values through the shared constant and runtime owners. Smoothstep
uses typed arithmetic so descending edges retain WGSL behavior on every target;
equal const/override edges reject at shader/pipeline creation, even with a runtime
third argument. Saved operand IDs preserve evaluation order. FMA constants use
declared-width fused evaluation; runtime target operations retain WGSL's permitted
accuracy envelope. These additions do not enable f16.
`make test-specialization` exercises all three targets and failed publication;
`make -C runtime/c/native check-float-math` exercises 33 literal-oracle words,
effect order, binding canaries and early release through the public C runtime.
CPU Vulkan execution, Metal SDK and DXC compilation are separate developer checks.

## Browser and offline WGSL delivery

The C source rewriter uses the same parser, semantic types and numeric owner.
It preserves original WGSL and rewrites only admitted scalar `u32`
identifier/member division or remainder by an explicit power-of-two literal.
Disabled optimization still validates the module and preserves every source byte.
The adapter owns allocation; the compiler core retains caller-owned storage.

```sh
make -C runtime/c/compiler test-browser
runtime/c/compiler/build/doe-emit-wgsl-c input.wgsl output.wgsl
WASI_SDK_PATH=/path/to/wasi-sdk-34.0-x86_64-linux \
  make -C runtime/c/compiler test-browser-wasm
```

The optional C browser build uses the archive pinned in
[`config/toolchains.json`](../../../config/toolchains.json), including its
SHA-256 and byte count. Extract that verified archive and set `WASI_SDK_PATH`;
builds do not download dependencies. The Wasm reactor has zero host imports,
a 64 KiB source limit and a 64 MiB memory ceiling. Initialize its `_initialize`
export once before using the existing worker ABI. The package worker handles
both this reactor and the retained Zig artifact. The build emits a source/hash
manifest, audits the four adapter-owned job slots, and carries runtime notices.

Stage an explicit C package artifact without changing the shipped default:

```sh
WASI_SDK_PATH=/path/to/sdk node packages/doe-gpu/scripts/build-browser-compiler.js \
  --provider=c --output-dir=/path/to/package-stage
```

The staging directory contains `assets/`, trusted metadata under `src/`, and
`BROWSER_COMPILER_NOTICES.txt`; serve these with the package modules and example.
`DOE_BROWSER_COMPILER_WASM` selects this artifact for the existing package unit
test. `DOE_WGSL_EMITTER` selects the C offline executable for the existing browser
acceptance harness. Source admission and worker checks do not establish GPU
execution; the browser remains the GPU provider for this delivery path.

## Build and decisive checks

The same C CLI also emits MSL and HLSL through the public typed target APIs:

```sh
make -C runtime/c/compiler test-emit-targets
runtime/c/compiler/build/doe-emit-msl-c --shader-path input.wgsl --out fresh.metal
runtime/c/compiler/build/doe-emit-hlsl-c --shader-path input.wgsl > output.hlsl
runtime/c/compiler/build/doe-emit-spirv-c --shader-path input.wgsl > output.spv
```

These explicit C executable names preserve the retained emitter input/output
arguments; omitted `--out` writes to stdout. The main `doe-compiler-c` executable
accepts `--target spirv|msl|hlsl`, defaulting to SPIR-V. Files use its existing
atomic no-clobber publication: an existing destination is preserved. Text emission
measures and fills through the same whole-module validation and target owner;
diagnostics keep original source locations. HLSL bundles select a source-order entry
with DXC `-DDOE_HLSL_ENTRY=0` (or the desired index) and its declared entry name.
Entryless text modules emit no text;
SPIR-V output requires an executable entry. The CLI accepts up to 2 MiB of source, matching the retained offline emitters.
Legacy ABI1 reflection retains its 64 KiB library admission limit; typed CLI paths
use caller-sized source storage. Other declared compiler pool limits still apply.
Legacy reflection modes require SPIR-V. Runtime SPIR-V mode accepts up to 64 finite
override values through ABI14, preserving every compute and graphics entry:

```sh
runtime/c/compiler/build/doe-emit-spirv-c --shader-path input.wgsl \
  --mode vulkan-compute-runtime --overrides SCALE=2,7=4 --out fresh.spv
```

An `@id` declaration uses its canonical decimal ID; other declarations use their
name. Repeated `--overrides` flags accumulate values; duplicate/unknown keys and
missing required values reject before output publication. Booleans use 0/1.
Unlike the retained Zig emitter, missing values accessed only by a graphics sibling
also reject, preserving the shared C semantic contract. This mode does not accept
legacy reflection options. Native DXIL,
CSL and TSIR delivery remain separate retained-consumer work; this adapter does
not substitute for native SDK validation or device execution.

From the repository root, ordinary source consumption is:

```sh
make -C runtime/c/compiler all
runtime/c/compiler/build/doe-compiler-c --shader-path input.wgsl --out fresh.spv
```

The C11 library and CLI use GCC, strict warnings, `-fno-fast-math`,
`-ffp-contract=off`, `-frounding-math`, and libm. They invoke no Zig, compiler
subprocess or fallback. Qualified native ABI: Linux x86-64, GCC/G++ 14.2,
C11/C++17. Other platforms/toolchains need their own qualification.

The Windows x64 DLL build uses an actual SDK developer environment and
`clang-cl`, retaining the existing Clang x64 floating-point control owner and
strict arithmetic flags:

```powershell
python runtime/c/compiler/tools/build_windows.py --out C:\doe-build\compiler
```

The output must be outside the source owner. The build uses the same source
inventories and export map, then runs ordinary C/C++ DLL consumers and concurrent
floating-point restoration controls. It does not build the POSIX CLI or establish
Windows runtime, target-device or full numerical qualification. The
[source-bound Windows host workflow](../../../.github/workflows/c-windows-host-build.yml)
retains first failures and exact artifacts; its existence alone is not a pass.
The [Windows host receipt](DEVELOPER_RECEIPT_WINDOWS_HOST.json) records the actual
SDK-built DLL and four passing CPU consumers at its pinned source revision,
including both preceding failures. [Selected terminal](WINDOWS_HOST_TERMINAL.txt)
is retained separately from the full artifact archive. This evidence covers the
standalone DLL host boundary, not later platform changes or full numerical/runtime
qualification; the first run did not record an SDK version.

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

The [canonical export map](src/exports.map) owns the standalone public symbols;
platform build recipes derive their linker lists from it. Retained ABIs 1–7,
[ABI10 Metal text](include/doe_compiler_text.h),
[ABI11 HLSL](include/doe_compiler_hlsl.h), and
[ABI12 Metal bindings](include/doe_compiler_metal.h) share one typed pipeline.
See [ABI4](include/doe_compiler_typed.h), the
[frozen numeric semantics](MILESTONE_12.md) and [lowering ledger](MILESTONE_12_LEDGER.md)
for retained storage ownership.

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

Fragment `discard` converts the invocation to a helper without terminating its
control flow. The shared statement/behavior owner preserves subsequent local
computation and returns; source-static entry closures reject discard reached
from compute or vertex entries, including skipped and dead syntax. SPIR-V 1.3
uses `SPV_EXT_demote_to_helper_invocation`, capability 5379 and opcode 5380,
never early `OpKill`. Native admission must explicitly enable the corresponding
Vulkan feature; successful validation alone does not establish device support.
Scalar/vector `ceil` uses the existing numeric overload and constant owners.
`make test-discard` checks these paths and emits the unchanged Gaussian splat
vertex/fragment source. Rendering and application qualification remain separate.
Rules: [WGSL 2026-10-21](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#discard-statement)
and the [Khronos demote extension](https://github.khronos.org/SPIRV-Registry/extensions/EXT/SPV_EXT_demote_to_helper_invocation.html).

Scalar `atomic<i32>` / `atomic<u32>` types are admitted in storage read_write and
workgroup roots, including arrays and structures. Atomic-containing types have
normal source layout but cannot be loaded, assigned or constructed by value.
All eleven standard scalar atomic builtins use direct address expressions and
the shared access-path owner; first-class pointer values/formals remain separate.
Dynamic OOB paths are guarded before pointer formation and atomic execution:
loads/updates yield zero, stores are discarded, and compare-exchange yields
`old_value=0, exchanged=false`. Address indices and value arguments execute once
in source order. Every atomic result is nonuniform for collective analysis.

The GLSL450 target uses Relaxed memory semantics with Workgroup scope for
workgroup objects and Device scope for storage, as prescribed by
[WGSL 2026-10-21 memory scopes](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#scoped-operations)
when the Vulkan memory model is not enabled. CompareExchangeWeak uses strong
compare-exchange, a permitted weak realization without spurious failure. Its
inferred two-field result remains a typed composite; no source-visible synthetic
type spelling is invented. Existing per-entry workgroup zero initialization and
barrier ordering are preserved. Scalar compute atomics require no additional
SPIR-V capability beyond Shader; native fragment writes retain device feature
requirements. The optional atomic_vec2u_min_max extension is not admitted.

ABI9 adds reflection kind7 ATOMIC in the unchanged typed record. Its element
names the i32/u32 scalar, size/alignment are four, and constructible is false.
Older selected reflection rejects atomic-reachable graphs; binary-only paths
share the general pipeline without that projection limit. Query sizes and public
export counts are unchanged. `make test-atomics` covers semantic/SPIR-V and ABI
checks, including both unchanged workgroup-atomic source witnesses. Normal/PIC
stack audits count the consolidated recursive call owner, not superseded TUs;
no scratch pool or recursive ceiling was increased.

Other barriers, general pointers, aliases,
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
- builtin.c / sema_call.c: shared overload identity, typed snapshots and builtin constants.
- image.c / sema_image.c: typed handle resources and image overload rules.
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

## Function-address pointer increment

The shared grammar/typed pipeline admits `ptr<function,T>` parameters and ordinary
address/dereference expressions, including immutable pointer aliases and forwarded
helper arguments. Pointees must be concrete constructible types. Pointer values
cannot escape through returns, mutable variables, structs, arrays or resources;
vector components are not addressable. Baseline arguments retain their root's
whole memory view. Private module variables and the `unrestricted_pointer_parameters`
and `pointer_composite_access` extensions remain unsupported, not inferred from
SPIR-V acceptance. The unchanged `monte_carlo_paths_fixed_samples.wgsl` is a source
admission witness, not a performance or physical-execution claim.

`TYPE_POINTER` and `IR_ADDRESS` are private typed identities, with no new public ABI,
reflection kind, record growth or queried workspace increase. A local address keeps
its root and access path, including once-evaluated dynamic indices; pointer formals
refer to the actual caller's Function variable. Subobject addresses are not converted
into target pointers until an ordinary guarded read/write, preserving per-access
OOB zero/discard behavior. Whole-root helper arguments use ordinary Function
pointers without either VariablePointers capability or a copy-in/copy-out substitute.

Source-static alias analysis propagates pointer parameter reads/writes across the
complete call graph, including skipped/dead calls. Read-only alias arguments are
legal; a repeated root with any written corresponding parameter rejects. One byte
per formal borrows the already charged analysis union after successful semantic
Walk completion, before closure/uniformity phases reset it. The checked work budget
is 67,108,864 operations; no unbounded hidden matrix/worklist is allocated.

A function whose transitive closure combines first-class pointer operations and a
collective currently rejects explicitly: pointer write-through uniformity requires
additional dependency summaries beyond the current return-value analysis. This
increment does not misclassify a uniform pointer identity as uniform pointee data.
Independent sibling entries without that interaction remain usable. `test_pointers.py`
checks source-static alias/lifetime rules, guards and actual emitted scalar helper
execution; `test_pointer_api.c` checks ABI7-9 projection/failure/reuse behavior.

`test_pointer_vulkan.c` consumes `tests/pointer_operations.wgsl` through the real
ABI9 library and the C Vulkan backend. Run `make test-pointer-vulkan
SOFTWARE_VULKAN_ICD=/path/to/software-icd.json`; the compiler Makefile delegates
archive compilation to the Vulkan owner and passes the explicit software opt-in.
`test-cpu-vulkan` also runs the atomic consumer. The pointer consumer's five
invocations compare 55 integer words, including actual
aggregate caller mutations, immutable index snapshots and OOB behavior. It requires
a software adapter and checks sentinels and pending lifetimes; no native runtime
parser, reflected execution or physical qualification is substituted.

## Explicit text artifacts, ABI10

`include/doe_compiler_text.h` adds `doe_compiler_text_requirements` and
`doe_compiler_compile_text`. The target-independent `compiler_analyze` owns the
same source preflight, grammar, typed validation, source closures, alias checks
and uniformity as ABI1-9. The selected Metal emitter consumes that IR directly;
zero SPIR-V words and target-declaration capacities are sufficient. Unsupported
Metal features fail explicitly without another parser, SPIR-V translation or
Zig fallback. The current target is MSL3.0, not a qualified Metal SDK artifact.

A checked ABI10 query appends one byte per maximum function/type identity, one
private staging record per resource/entry capacity, and the explicit `text_bytes`
pool to the retained stage workspace. ABI1-9 sizes are unchanged. Measurements
need no text staging bytes. Full code publication serializes privately, checks
all selected output spans and restores the one numeric environment before copying
any output. The metadata staging also prevents partial record publication if
name/slot serialization fails. Text is an exact byte span without a terminator.

Stage/resource reflection remains whole-module schema9. Sparse target bindings
name source resource indices once; the retained entry-reference spans determine
membership. Target entry records carry actual emitted Metal host names and
per-entry sizes-buffer slot/minimum byte span. Each runtime-tail size element is
an actual uint32 binding byte length indexed by the emitted buffer slot. This is
a narrow target contract, not complete retained TranslationInfo or device feature
authorization. Selected entries emit only their target closure while unused source
siblings still undergo complete source validation. Entryless NONE has no text.

`make test-text` checks public C/C++ use, graphics and resource mapping, exact
measure/capacity failures, sparse metadata, final FP-restore failure after real
serialization, reuse, and emitted C++ operations. Those host controls do not
qualify Metal address spaces, the SDK or a GPU. `audit_text_stack.py` retains all
normal/PIC frames, verifies GCC aliases against their actual object units and
charges all bounded Metal recursive families separately from source analysis.
The libc formatter is an explicit external leaf, not silently omitted storage.

Consumers build the canonical shared library with `make shared`, optionally
setting `SHARED_LIBRARY=/tmp/output/libdoe_compiler.so`, `TMPDIR=/tmp/output/tmp`
and `EXTRA_CFLAGS` for instrumentation. The compiler and private target checks
reuse `targets/metal/sources.mk` and `targets/hlsl/sources.mk`; consumers must not parse or copy source lists.
`make -s print-library-sources` exposes expanded paths for source/graph audits.

### Additive public HLSL text facade (ABI11)

[`doe_compiler_hlsl.h`](include/doe_compiler_hlsl.h) adds two typed entrypoints
without changing ABI1-10. It reuses the text API's capacities, flags, result and
one compile/measure/publication owner, with separate HLSL binding and entry
records. The actual serializer supplies register namespace/number/space/stride,
exact entry names, bundle selectors, per-entry wave requirements and any group-
count CBV. There is no reinterpretation of Metal's size-buffer metadata and no
caller-side target parser. Whole-module schema9 reflection stays authoritative.

`make test-hlsl-text` compiles and runs ordinary C/C++17 source-to-text consumers;
`TEXT_TEST_BUILD` and `SHARED_LIBRARY` select their fresh output paths/artifact.
The HLSL [target guide](targets/hlsl/README.md) specifies the bundle, binding and
native consumer obligations. `DOE_SHADER_HLSL` in the [shader owner](../shader/README.md)
retains these exact records with source/target identity and allocation rollback.
Typed host results alone supply no DXC, DXIL, Windows SDK, GPU or production support.
The [ABI11 developer receipt](DEVELOPER_RECEIPT_HLSL_ABI11.json) separately binds
five actual DXC compute compilations and reflected register/wave comparisons.

### Component numeric family (developer increment)

One signature/type/stage owner now includes `abs`, `sign`, `floor`, `round`,
`trunc`, `fract`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`,
`asinh`, `acosh`, `atanh`, `log`, `log2`, `exp2`, `inverseSqrt`, `degrees` and
`radians`. Scalars and vectors use the same rules; unsigned `sign` rejects.
`numeric_math.c` owns declared-width const math and domain checks. Runtime
values keep IR operations, not host evaluation. Evaluated nonfinite constants
fail, while skipped evaluation still validates names/types. Signed minimum
`abs` preserves its bit pattern, including AbstractInt. `round` is ties-even;
`fract` is subtraction from floor and can round to 1, not a clamped fraction.
Existing ABI records, queries, pools and public exports are unchanged.

Rules are bound to [WGSL 2026-10-21 numeric builtins](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#numeric-builtin-functions)
and [GLSL.std.450](https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html).
MSL uses `rint`, not `round`, and explicit fractional subtraction; angle conversion
uses rounded f32 factors. See [Apple MSL math functions](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf),
2026-06-04 section 6.5. HLSL uses its documented [ties-even round](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-round);
inverse hyperbolics explicitly reject until a qualified lowering exists.
Integer absolute-value target operations avoid signed-overflow assumptions.
The same component owner now admits two-argument `atan2(y,x)` and `pow(x,y)`
for matching scalar/vector shapes. Constant evaluation preserves AbstractFloat
versus f32 width, rejects negative pow bases even with integral exponents, and
retains skipped-expression/type validation. SPIR-V uses the corresponding
GLSL.std.450 instructions; MSL and HLSL use explicit target mappings. Ordered,
once-evaluated operand snapshots are checked with effectful arguments whose
reversal changes the answer. These developer checks do not qualify physical
transcendental accuracy or a complete target SDK surface.
Text mappings/host controls do not establish SDK acceptance or physical accuracy.
`test-component-math`, Metal `component-math-control`, and HLSL `host-control`
exercise the new owner. Both retained stable-fluids advection shaders compile
unchanged. At that numeric checkpoint, the retained 4096-iteration compile-stress
shader hit the separate phony-assignment (`_ = x`) boundary; it was not shortened.

### Transparent type aliases and Workgroup target identity

Module `alias` declarations now resolve through the same hoisted, iterative
constant/struct dependency graph. Scalar, aggregate, handle and admitted pointer
aliases preserve the underlying TypeId, constructor behavior and ordinary name
shadowing. Forward chains, mixed cycles, unused invalid aliases and duplicate
module names are checked without a new registry, allocation or public ABI.
Constants and aliases share the existing bounded module-declaration pool.

SPIR-V now plans undecorated Workgroup array/struct target types separately from
the explicit-layout buffer types. Whole aggregate reads/writes convert through
same-typed leaves with the existing descriptor/traversal/word budgets; guarded
out-of-range aggregate accesses retain zero/discard behavior. This repairs both
unchanged retained tiled-matmul sources without removing buffer strides or adding
a Vulkan extension requirement. `test-type-aliases` and `test-workgroup-types`
cover these paths, including a validator-valid wrong-member mutation rejected by
the retained word executor. Their portable job-API check covers declaration
exhaustion, atomic failure/reuse and floating-point environment restoration;
`sanitize-type-aliases` exercises that check with address/undefined sanitizers.
These are developer source/word checks, not physical
GPU, numerical or end-to-end migration qualification.

`shared SHARED_PLATFORM=Darwin SHARED_LIBRARY=/tmp/libdoe_compiler.dylib` prepares
Mach-O linking using an exported-symbol list generated from the same `exports.map`.
Its install name defaults to `@rpath/<output basename>`; `SHARED_INSTALL_NAME`
can override it explicitly without embedding a temporary build directory. The
Linux shared recipe remains ELF. The recipe and dry-run tests alone claim no
Darwin compiler build, Apple SDK execution or portability qualification. Libm
remains an explicitly external leaf in complete stack accounting, as with the existing exp/sqrt/tanh owner.

### Host floating-point environment

`numeric_environment.c` owns one per-call environment and exact cleanup; it does
not require a specific compiler version or operating-system brand. The numeric
translation units require IEEE binary32/binary64, declared-width evaluation,
subnormal support, two's-complement integers and strict finite-aware build modes.
Actual rounded-bit and input/output-subnormal probes run after normalization.
Unknown machine-control targets fail explicitly before semantic evaluation.

The x86-64 owner saves/restores MXCSR and the x87 control word alongside opaque
standard `fenv_t`. It normalizes x87 precision as well as masks/rounding because
libm may use x87 even when C expressions use SSE. The ARM64 owner separately
saves/restores full FPCR/FPSR and clears rounding, flush/alternate modes and trap
enables without assuming a platform `fenv_t` representation. The register owner
follows [Arm FPCR](https://developer.arm.com/documentation/ddi0601/latest/AArch64-Registers/FPCR--Floating-point-Control-Register)
and is cross-checked against [LLVM's ARM64 floating environment](https://github.com/llvm/llvm-project/blob/main/libc/src/__support/FPUtil/aarch64/FEnvImpl.h).
Clang uses its supported standard [floating-environment pragmas](https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior);
the canonical strict flags remain required for all builds.

Save/hold/round/clear/verification failures restore caller state. A final restore
failure still blocks publication after one bounded recovery attempt. Caller errno
remains the public adapter's responsibility. Focused checks include real libc
failure wrapping, x87 24-bit incoming precision, all rounding modes, exception
flags/traps, subnormals and independent threads. Current executed qualification
is Linux x86-64 GCC 14.2 only. The separately scoped
[Windows DLL developer controls](DEVELOPER_RECEIPT_WINDOWS_HOST.json) exercise
caller-state restoration and ordinary C/C++ consumption; they do not establish
full numerical qualification on Windows or other required host configurations.
Existing historical host restrictions/receipts are unchanged.

## Explicit standard uniform layout

Flags-bearing ABI4–11 calls accept `DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT`
in addition to selected output/measure bits. This opts the compiler into the
[WGSL standard uniform layout language feature](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#address-space-layout-constraints); it is accepted with or without
`requires uniform_buffer_standard_layout;`. The directive checks implementation
support and does not enable it. Calls without the flag, including ABI1–3, retain
conservative uniform admission. Native provider feature reporting is unchanged.

Natural type sizes, array strides, member offsets, explicit alignment/size and all
host-shareable/fixed-size restrictions remain canonical. Reflection reports
`DOE_COMPILER_TYPED_PROFILE_STANDARD_UNIFORM` and natural uniform address-space
alignment when selected. SPIR-V uses the existing baseline-compatible raw uniform
representation when native block layout cannot express those bytes, including
exact 4/8/12-byte tail-only blocks. No Vulkan uniform-layout feature is requested.
MSL/HLSL use the same semantic byte offsets. Public records, symbols, workspace
requirements and existing profile values do not change.

`make test-uniform-standard` checks opt-in/default/directive behavior, natural
reflection, nested/attribute/matrix layout and failure publication. An optional
output directory argument to its binary emits SPIR-V/MSL/HLSL developer artifacts.
`test-uniform-standard-vulkan` takes `SOFTWARE_VULKAN_ICD` and
`UNIFORM_STANDARD_ARTIFACTS`; it checks exact-size CPU descriptors and a well-typed
wrong-offset SPIR-V control. `test-uniform-standard-host` consumes the emitted MSL
with erased address spaces/attributes; it is not MSL SDK or GPU qualification.
Independent semantic acceptance and physical qualification remain separate.

## Core binding identifiers and Metal ABI12

`DOE_COMPILER_CORE_BINDING_NUMBERS` is an explicit semantic flag for existing
flags-bearing APIs and ABI12. It admits group0..3/binding0..999 as logical source
identities. Without it the earlier binding0..31 admission remains unchanged.
This is independent of resource counts, work budgets and physical argument slots;
it is not a WGSL `requires` extension. No compiler pool or limit is enlarged.

`doe_compiler_metal_requirements` and `doe_compiler_compile_metal` add ABI12 without
changing ABI1–11 records or requirements. Metadata has exact per-entry binding
spans, independent dense native namespaces, copied entry names and canonical
runtime-tail size-table identity. ABI10 remains the explicit fixed-slot contract.
`make test-metal-entry-text` covers measure/publish symmetry, source-default versus
opt-in policy, namespace overflow, caller-byte preservation and FP/errno state.
The emitted helper host control distinguishes conflicting entry mappings without
claiming native Metal execution. See [the target contract](targets/metal/README.md).

## Reflection direction builtin

`reflect(incident, normal)` now shares the existing floating-vector overload and
ordered argument owner across constant evaluation and SPIR-V/MSL/HLSL output.
It accepts admitted vector2/3/4 floating types and preserves the supplied normal;
there is no implicit normalization. The const path follows the
[WGSL reflection definition](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#reflect-builtin)
through existing dot and declared-width numeric operations. Runtime SPIR-V uses
the registered [GLSL450 Reflect instruction](https://raw.githubusercontent.com/KhronosGroup/SPIRV-Headers/main/include/spirv/unified1/extinst.glsl.std.450.grammar.json);
MSL/HLSL use their explicit reflection intrinsic mappings.

`test_component_math.py` covers constant values, overload rejection, shadowing and
ordered runtime arguments. `make test-reflect` composes the public compiler target
interfaces; its optional output directory emits fresh target artifacts.
`test-reflect-vulkan` requires an explicit `SOFTWARE_VULKAN_ICD` and `REFLECT_SPIRV`
artifact, comparing exact words for operand reversal, non-unit normals and
effectful storage snapshots. These developer controls do not qualify physical
execution or close the separate refraction, face-forward and full language gates.


## Ternary geometry builtins

`faceForward(normal, incident, reference)` and `refract(incident, normal, eta)`
use the same signature/materialization and ordered argument owners as the other
numeric builtins. Refraction's third operand is a scalar of the vectors' common
floating leaf type; neither geometry path normalizes its inputs. Const evaluation
uses declared-width dot/arithmetic/sqrt primitives, testing the refraction
discriminant before square root so total internal reflection returns zero.
Face-forward negates the first operand when the dot product is zero or positive.
See the WGSL [face-forward](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#faceforward-builtin)
and [refraction](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#refract-builtin) contracts.

`make test-geometric` exercises the public SPIR-V/MSL/HLSL interfaces, including
caller FP/errno state. The resulting `test_geometric` executable accepts an
optional existing output directory for fresh target artifacts. The separate
`test-geometric-vulkan` route takes `SOFTWARE_VULKAN_ICD` and `GEOMETRIC_SPIRV`,
comparing 39 fixed expected words for strict-zero face direction, operand roles,
negative/half/total-internal eta cases, non-unit normals, once-only argument
effects and storage snapshots. `test_component_math.py` independently checks
const vector2/3/4 values, runtime instruction operands, overload rejection and
shadowing. SDK compilation and CPU Vulkan controls remain developer evidence;
they do not qualify physical execution or close f16/full-language migration.

## Binary16 developer surface

See [binary16 semantics, focused checks and remaining gates](F16.md) for the
shared native C f16 path. Native-device admission remains feature-scoped; target
validation does not establish physical backend qualification.

[ABI16 sampling relationships](SAMPLING.md) adds explicit, failure-atomic
per-entry texture/sampler pairs to specialization without changing frozen records.

[Floating decomposition](DECOMPOSITION.md) covers the shared `frexp`, `modf` and
`ldexp` semantics and focused target/constant controls.
