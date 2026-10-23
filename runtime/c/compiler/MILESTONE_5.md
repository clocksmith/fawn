# C compiler milestone 5: flat uniform resources and explicit job storage

Frozen before implementation on 2026-10-23 against accepted M4 commit
`410c181a0db3787f3c6fb4e9b2c1a839ddfb4a75`. This is one resource-layout and
compiler-job ownership increment, not a complete WGSL compiler/runtime migration.

## Selection and unchanged consumer

The immutable [M4 ledger](../../../docs/c-migration/corpus-coverage-m4.json)
contains 98 paths / 91 unique contents, four admitted paths / three unique
sources. Inspection finds 69 uniform paths / 64 unique contents, all using flat
u32/f32 structs: 61 four-member blocks, seven eight-member blocks and one
three-member block. This is a useful general prerequisite, not 69 newly complete
kernels. Other features still block most sources.

The new end-to-end witness is unchanged
`bench/kernels/package_vector_scale_add.wgsl`, SHA-256
`8f2b3e2bb6955c1f7507936773589195c80e6a119cbd50d975f652d4f834e8c1`.
Compile through the native API/CLI and unchanged
`bench/gates/spirv_val_gate.py:emit_spirv_for_wgsl` protocol. Preserve the M4
artifacts and the new witness's pre-change failure. This does not qualify the
package's actual source-to-GPU path or its runtime adoption.

## General declarations and typed member access

Retain all M4 admitted source behavior. Add, in the module declaration section
before the single entry function:

- Zero to eight named flat structs, each with one to eight named members of
  exactly u32 or f32. Comma-separated members, optional trailing comma. No
  struct-level semicolon. Resolve references independently of declaration order;
  types can be reused by multiple resources. Reflect all declared structs and
  members, including unused ones, in declaration order.
- `@group(G) @binding(B) var<uniform> NAME: TYPE;`, with the same attribute
  ordering/number admission as retained storage declarations. Uniform and storage
  resources together admit one to eight bindings, including uniform-only modules;
  zero-resource modules are not introduced. Duplicate coordinates reject
  across both kinds. Uniform access is read-only and the type must be a declared
  flat struct. Names and numbers retain all prior lexical bounds/rules.
- `RESOURCE.MEMBER` typed scalar loads wherever M4 admits a scalar expression.
  u32 member loads additionally serve as an M2 index base or either condition
  operand. The index's optional +/- offset and the condition's nonmember RHS
  remain literal/module-constant only. No arbitrary expression-index/condition
  grammar expansion. f32 members cannot be used as indices/u32 comparisons.
- Local snapshots retain M4 evaluate-once and source-order behavior. Member
  loads are runtime values for M4's f32 materialization rule. Resource/member
  identity comes from validated declarations, never special Params/count names.
  All declared fields, including names resembling padding, are real fields.

Reject duplicate types/members/module names, unknown types/members, writes to
uniform resources, aggregates used as scalars, invalid visible-name/type/builtin
resolution, mixed scalar operations, and all earlier invalid access/type/scope
cases. Member names are scoped to their owning type and may repeat in other
structs. New types/resources join the existing module namespace and no-shadowing
rules; builtin spellings are not globally reserved, but a visible user declaration
must not be bypassed during type/builtin resolution. Validate dead source too.

Explicitly unsupported: nested structs, arrays/vectors/matrices inside uniforms,
scalar uniform declarations, aggregate values/copies/constructors, layout
attributes including @align/@size, aliases/pointers, i32/f16/bool/atomics,
additional entrypoints, loops/helpers and other previously excluded language.
Some exclusions are valid WGSL outside this declared subset, not invalid WGSL.
No filename/source-pattern recognizer or second parser/emitter is allowed.

## Layout, reflection and target preconditions

Freeze WGSL's conservative uniform profile without
`uniform_buffer_standard_layout`. For this subset u32/f32 size/alignment is four;
member i has byte offset 4*i; a struct's natural alignment is four and size is
4*member_count. Uniform address-space aggregate alignment is separately 16.
A three-member struct has size/minimum binding extent 12, not 16. A storage
runtime scalar array has element stride four and minimum binding extent four
(one element), not zero. Include unused fields in the uniform minimum extent.
See [WGSL size/alignment](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#alignment-and-size),
[uniform constraints](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#address-space-layout-constraints)
and [WebGPU minimum binding size](https://gpuweb.github.io/gpuweb/#minimum-buffer-binding-size).

New v2 declaration reflection separates resource kind/access, scalar storage
stride/runtime sizing, minimum binding extent, natural and address-space
alignment, and a struct index. Struct records contain all declaration-ordered
member names/types/offsets/sizes/alignments. All declared resources remain
reflected, including unused ones; this is not precise per-entrypoint liveness.
Storage struct_index is DOE_COMPILER_NO_STRUCT; uniform scalar/element_stride/
runtime_sized are zero. Every unused result record/field is zeroed. Type and
resource counts are exact. The header owns the native fields and enum values.

Emit typed Uniform-class Block structs with explicit member Offset and resource
DescriptorSet/Binding decorations and fixed-index accesses. Never use
OpArrayLength for uniforms. Reused types do not merge separate resource identity.
No optional scalar/uniform-standard-layout Vulkan feature is assumed. Preserve
per-access storage OOB-load-zero/OOB-store-discard guards and wrapped indices.
Logical empty-storage oracle trials are not physical zero-byte binding claims.
Uniform buffers must contain at least their whole reflected minimum before
invocation; undersized uniforms are a host/dispatch admission failure, not a
compiler-generated zero-fill promise. Device descriptor-offset alignment,
usage/ranges/lifetime/limits remain runtime preconditions, not the reflected
natural or address-space type alignment. No physical execution is claimed.

## Workspace and native ABI compatibility

Keep include/doe_compiler.h byte-identical, ABI1 symbol/layout/status semantics,
storage-only admission, no heap allocation/free and no retained pointers. ABI1
rejects new struct/uniform declarations explicitly, including unused structs;
it cannot silently omit unrepresentable reflection. One shared typed pipeline
handles both APIs, parameterized only by admitted capability. No fallback.

The separately versioned include/doe_compiler_v2.h adds size/alignment query and
compile_v2. Requirements are current-library dependent, not stable serialized
layout. A caller supplies malloc/aligned-allocation storage of at least the
queried size/alignment, no more alignment than max_align_t. Arbitrary declared
char arrays are not promised a portable typed-object contract. Caller allocates
and frees; library never allocates/frees, retains pointers, or requires explicit
workspace initialization/destruction. One active call per workspace; distinct
workspaces support concurrent calls. Workspace is reusable after every success
or failure without reset. Its post-call contents are unspecified.

Source/word/result/workspace valid spans must be mutually disjoint. Preflight
ABI/result-size and workspace size/alignment before touching workspace. Wrong
ABI/exact result size leaves all output and workspace bytes unchanged. NULL
result returns INPUT_ERROR without writing. Other failures preserve every word,
zero result word_count/reflection, and report original-source diagnostics.
Invalid workspace produces INPUT_ERROR at offset zero before workspace access.
Query NULL output is INPUT_ERROR; wrong version/exact size is ABI_MISMATCH with
no query-output mutation. Query/compile support only v2; ABI1 supports only v1.

Use actual phase lifetimes to share typed scratch: no token pointer survives
parsing, so tokens may overlap later typed IR/target storage. ABI1 owns an actual
private typed workspace local. Its measured GCC14.2 -O2 frame must not exceed
M4's 105,328 bytes. The caller-workspace entrypoint/core may not hide another
large stack workspace: per-function static frame <=8,192 bytes (ABI1 wrapper
excepted). Report parser/lowerer/emitter recursive/callee frames and limits,
workspace size/alignment and all new result sizes. These are per-frame budgets,
not a measured combined stack high-water mark. Retain source/token/name/word,
statement/expression/depth/IR capacities unless a checked derivation proves the
existing IR bound needs amendment before implementation. No silent bound growth.

## CLI and JSON compatibility

- Binary-only mode uses v2 and the unchanged required --shader-path/--out
  protocol, with no JSON allocation/check/temp/publication when absent.
- Existing --reflection JSON selects storage-only ABI1 admission and preserves
  existing JSON bytes/schema. Uniform input fails without either output rather
  than publishing a binary with omitted metadata.
- Add mutually exclusive --reflection-v2 JSON, at most once and nonempty. Its
  JSON has schema_version:2, entry_point, workgroup_size, binding_count, bindings,
  struct_count and structs. Binding keys exactly match v2 field names; kind is
  "storage_buffer"/"uniform_buffer", access "read"/"read_write", scalar is
  "u32"/"f32" for storage and null for uniform; struct_index is null for storage.
  Struct/member keys match the v2 header; member scalar is "u32"/"f32". Array
  lengths equal counts, runtime_sized is numeric 0/1, integer values are decimal,
  and no unused slots are serialized.
  JSON whitespace/order is deterministic; compare parsed v2 objects for schema.
- Add the new option to recognized-option-as-value rejection. Existing arguments,
  path/no-clobber/owned-cleanup and one/two-file ordinary rollback stay unchanged.
  The same M4-admitted storage source emits identical SPIR-V in all three modes.
- CLI owns queried workspace allocation/free and, where needed, v2 result/JSON
  storage; allocation failures are explicit CLI failures before publication.
  V2 JSON checked capacity is 32,768 bytes: header <=512, eight bindings <=8*768,
  eight structs <=8*512 and 64 members <=64*256, including fixed keys, maximal
  63-byte ASCII identifiers, punctuation and ten-digit u32 integers, total
  <=27,136. Every append still checks remaining space; no truncation.
  Legacy JSON retains its existing bound/bytes. No crash-atomic/durability claim.

## Acceptance and bounded effort

Freeze before editing implementation. One implementation owner plus independent
evaluator; one coherent resource/job increment, not a parallel runtime rewrite.
Preserve first failures and distinguish evaluator errors from candidate defects.
Run focused retained M4 semantic/ABI/CLI checks, classifying each intentional
negative lift against this grammar; no broad relaxation. Existing immutable
historical suites/results remain unchanged.

- New unchanged source through fresh C source build, prebuilt native consumption,
  and unchanged emitter function; independently validate as Vulkan1.1 and execute
  emitted opcodes for count 0/1/last/above-length/UINT32_MAX, unequal storage
  lengths, nonzero output sentinel, unchanged read-only inputs/tails and y/z.
- Independent layout/oracle trials for 1/3/4/5/8 fields, mixed types, all fields,
  declaration reorder/renaming, multiple resources/shared types, unused fields,
  local snapshots and member indices/conditions. Use independently packed raw
  bytes and emitted offsets, never reflection-driven arithmetic substitution.
  Reject invalid short uniform bindings before interpretation. Test wrong offset,
  member/type/resource/binding mutation controls, not just valid module magic.
- Native C and C++ clients: ABI1 sizes/offsets unchanged; v2 widths/sizes/offsets,
  malloc workspace, exact/short/misaligned/NULL/oversized buffers, version/size
  preflight no-write, reuse after each failure, independent parallel workspaces,
  guard pages, all-output atomicity and no library allocator calls.
- Negative syntax/type/scope/layout/binding limits; exact capacities, dead-code
  validation, malformed-source fuzzing, ASan/UBSan and CLI allocation/I/O fault
  cleanup in all relevant modes. Query failure must not cause an allocation.
- Source-only portable independent checks, current source/artifact hashes,
  strict C11 warnings, build/size/stack receipts, exact command/tool provenance.
  Keep library export allowlist to the old symbol and two declared v2 symbols.
- Update same 98-source ledger with unique-output unchanged-consumer calls,
  source/emission/validation/execution and backend gates separated. No full
  gate-main rerun or inference from its successful exit/EMIT-SKIP behavior.

If a frozen bound/compatibility promise cannot be met, report the exact conflict
before changing it. No extra tool install, physical machine, public push or
production Zig edit. No production owner is replaced by passing this increment.
Production TranslationInfo/cache identity/entrypoint overrides/other targets,
actual application consumers and physical backend gates remain separately open.
