# Experimental standalone C compiler

This isolated [C migration](../../../docs/c-migration/MIGRATION.md) compiler is not
a production compiler replacement or a complete WGSL implementation. The current
scalar numeric/constant owner is frozen in [M8](MILESTONE_8.md), extending the
retained [M7 function/control owner](MILESTONE_7.md) and its
[empty-declaration](MILESTONE_7_AMENDMENT_1.md)
[amendments](MILESTONE_7_AMENDMENT_2.md). Earlier contracts and acceptance receipts
remain immutable. [Independent M8 acceptance](MILESTONE_8_RESULTS.md) passes for
the declared surface; it does not establish physical GPU qualification or full
migration parity.

## Build and use

Use the installed Linux x86-64 GCC14.2 toolchain and an already installed validator:

```sh
make all
make test-scalar SPIRV_VAL=/path/to/already-installed/spirv-val
make sanitize-scalar
build/doe-compiler-c --shader-path input.wgsl --out fresh.spv
build/doe-compiler-c --shader-path input.wgsl --out fresh.spv --reflection-v2 fresh.json
```

The Makefile builds C11 directly, with strict warnings, `-fno-fast-math`,
`-ffp-contract=off`, `-frounding-math`, and `-lm`. There is no Zig invocation,
fallback, compiler subprocess or hidden allocator in the library. Numeric code
uses bounded integer literal parsing plus qualified host binary32/binary64
arithmetic and truncating `fmodf`/`fmod`; it never uses locale-sensitive numeric
parsing. Floating environment operations are the other libm dependencies.

`build/libdoe_compiler.so` exports only the five existing functions:
`doe_compiler_compile`, `doe_compiler_compile_v2`,
`doe_compiler_workspace_requirements`, `doe_compiler_job_requirements`, and
`doe_compiler_compile_job`. The version script localizes private cross-file
symbols. Public ABI1, v2 and job headers, enum values, result layouts and JSON
schemas are unchanged.

CLI `--shader-path` and `--out` are required exactly once. Reflection is optional:
no option means no JSON allocation or sidecar; `--reflection` selects the fixed
storage-only ABI1 projection; `--reflection-v2` selects the configurable job and
v2 projection. The two options are mutually exclusive. Paths must be nonempty;
an option token cannot substitute for a missing path. Use `./` when a filename
itself equals an option. Existing destinations, including symlinks, are refused.
A failed second publication rolls back the first owned publication, but this is
not a crash-atomic multi-file transaction. The existing
[emitter consumer](MILESTONE_3.md) still uses only `--shader-path` and `--out`.

## One grammar, three allocation adapters

All APIs share the lexer, parser, scalar/type/constant semantics, typed IR and
emitter. Fixed APIs do not have a separate grammar dialect. A valid new construct
can still exceed an actual fixed capacity. Uniform/resource reflection admission
is also explicit: ABI1 cannot publish uniform structs, whereas v2/job can.

- Fixed ABI1/v2: source65536, tokens2048, statements16, charged expressions320,
  expression/control depth18/4, IR385, value-map386, staging words4096.
- Job: caller-selected source/token/statement/expression/word capacities, with
  expression depth1..64 and control depth1..32. Checked derived storage is
  `I=1+4E+8S` instructions, `I+1` semantic values, `S+1` locals/scope records,
  `C=E+1` module constant records and `E+1` physical expression slots.
- Reflection bounds remain names63, resources8, structs8, members8, inputs5.
  Groups remain0..3, bindings0..31, workgroup dimensions/product1..256.

The fixed adapters retain exact historical accounting exceptions: the leading
unannotated global-invocation-x alias consumes no statement/node; an admitted old
parenthesized if-comparison wrapper is uncharged; the exact first nonempty
`const NAME:u32=DECIMALu;` initializer is uncharged. Bare decimal attribute values
remain uncharged. The first-constant exception applies to jobs too. These are
compatibility accounting rules, not source recognition or distinct semantics.

Word exhaustion is OUTPUT_TOO_SMALL; other checked storage/count/layout exhaustion
is INPUT_ERROR. Required scalar type/range errors are SEMANTIC_ERROR; unsupported
families remain UNSUPPORTED. Source offsets fit UINT32_MAX-1. Every slice checks
count multiplication and alignment before binding caller storage. The target ID
bound is `256+32I`; per-operation reservation and word serialization also check
bounds independently. These safe envelopes are not permanent full-compiler
architecture or unrestricted-module claims.

ABI1 owns a real typed stack workspace. V2 queries current size/alignment and uses
caller allocation. Jobs query from capacities before caller allocation. The
library never allocates/frees, retains caller pointers, or needs reset/destruction.
Source, workspace, output and result spans must be disjoint; concurrent calls use
separate workspaces. ABI/version/exact-result-size mismatch touches neither result
nor words. Other failures zero result count/reflection and preserve all caller
words. The workspace is reusable after failure. Private queried layout is not a
serialized ABI.

## Scalar numeric semantics

Supported scalar values are bool, i32, u32, f32, signed64 AbstractInt and finite
binary64 AbstractFloat. Abstract values remain const-stage values until actual
WGSL overload/materialization rules select a concrete type. Unannotated consts
retain abstract identity; unannotated let/var defaults are i32/f32. Let reads are
runtime-stage snapshots, even with a const initializer. No implicit concrete
numeric conversion or numeric truthiness is introduced.

The shared grammar admits decimal/hex integer literals and decimal/hex/exponent
floating literals, unary `! - ~`, scalar `+ - * / %`, comparisons, max, integer
`& | ^`, eager bool `& |`, short-circuit `&& ||`, shifts, zero/one-argument scalar
constructors, and numeric scalar bitcast. WGSL grouping rules apply, not C's
bitwise precedence: mixed operator groups and non-unary shift operands require
parentheses. Concrete f-suffixed hexadecimal literals require exact binary32
representation. Unsuffixed floats are evaluated at binary64 source boundaries,
not prematurely folded at f32. See M8 for literal precision and the documented
dated-spec example inconsistency.

Const operations apply their actual operator-specific rules: concrete integer
addition/subtraction/multiplication wrap, abstract overflow rejects, division and
shift errors are stage-sensitive, and explicit conversions are not interchangeable
with automatic materialization. Float-to-integer clamps use source-float
representability, including the different AbstractFloat versus f32 endpoints.
Const nonfinite intermediates reject. Skipped const RHS numeric evaluation is
suppressed, but names, types, dependencies and mandatory operand checks remain.

Runtime integer division/remainder select a safe denominator before the target
operation, handling zero and signed-min/-1 without an invalid intermediate.
Runtime shift counts are masked before shifting. Float-to-integer conversion
normalizes NaN and clamps finite/infinite inputs before OpConvert. Runtime bitcast
preserves payloads in the emitted instruction/model; this is not a universal
physical-device NaN/subnormal payload guarantee. Each f32 source arithmetic
boundary remains a distinct target operation; there is no reassociation pass.

Every compilation saves, holds, normalizes and restores the calling thread's FP
environment, including rounding, exception flags/traps and SSE FTZ/DAZ. Fallible
restoration finishes before the sole output-word publication. Unexpected host
restoration failure is explicit and preserves output, but cannot claim the caller
state restored. Only the documented Linux x86-64 IEEE/GCC host is qualified by
these developer receipts; other hosts need new representation, fenv, stack and
accuracy evidence.

## Constants, state and resources

Module and local scalar const declarations share expression semantics. Module
constants can be interleaved with resources, flat structs and the single compute
entry, including after the entry. Module names are hoisted; all initializers,
including unused and skipped dependencies, are validated. Forward references and
cycles use iterative caller-owned dependency state, never declaration-count C
recursion. Local names become visible after initialization and use nearest-scope
lookup; same-scope duplicates reject. Constructor/bitcast/type names respect that
lookup instead of bypassing user declarations by spelling.

Group, binding and workgroup attributes accept integer const expressions and
forward module constants, with checked final limits/products. Compute inputs are
zero to five unique builtins, independently named/ordered: global/local invocation,
workgroup and num-workgroups vec3<u32>/vec3u scalar x/y/z, plus scalar u32
local_invocation_index. Every declared input enters the target interface; none is
invented for empty modules.

All adapters admit scalar var/let/const, assignment/compound updates, integer
increment/decrement, blocks, if/else, for/while/loop, continuing/break-if,
break/continue and return within their real limits. Function variables are
declared at target entry but initialized at their source execution point each
iteration. Scope and behavior validation include dead source. Continuing cannot
read a declaration bypassed by a targeted continue. The emitter suppresses dead
operations, tracks actual merge predecessors, and never adds a second terminator.
Harness step limits are observation bounds, not shader termination rules.

Public resources remain u32/f32 storage runtime arrays and flat u32/f32 uniform
structs. i32/bool locals and consts do not reinterpret frozen reflection enums;
i32 resources remain unsupported by this reflection owner, and bool is not
host-shareable. Signed runtime indices have an explicit nonnegative guard before
the per-resource unsigned length guard. Each robust load independently yields
zero out of bounds; each store independently discards out of bounds. An OOB inner
load's zero may legitimately select a valid outer access.

Uniform member offsets are4*i, scalar size/alignment4, struct size4*members and
natural alignment4; uniform address-space aggregate alignment is16. A three-member
minimum is12, not16. Uniform minimum extents and descriptor/lifetime/device rules
are host dispatch preconditions, not short-buffer zero-fill. V2 reflection includes
all declared structs/resources in order. Logical zero-element model buffers are
not physical zero-byte binding claims.

Still outside this owner: f16, arbitrary vectors/matrices, user helpers, sqrt/exp/
clamp and other new math, collectives, workgroup memory, textures and additional
resource layouts. The six unchanged M8 witnesses compile and validate during
developer smoke; filenames containing f16 do not establish an f16 compiler owner.

## Storage, stack and verification

GCC14.2 -O2 measurements: LegacyWorkspace76512 bytes/alignment8, Syntax2696,
Expression40, ConstantSyntax40, LocalBinding120. ABI1 frame76592 is below the
frozen77824 ceiling; all other library frames are <=1344 (ceiling8192).
Checked recursion depths remain18/4 fixed and64/32 job, with compound parser
ceiling2*control_depth+1. Conservative maximum-job recursive-path sums are under
35KiB parse,87KiB semantic lowering and22KiB emission, excluding caller frames.
These are derived call-path bounds, not measured stack high-water marks. Larger
capacity allocations do not authorize greater recursion. Other toolchains need
fresh measurement.

`make test-scalar` checks numeric values, exact materialization/stage cases,
emitted signed/numeric instructions, safe-operand guards, namespace/dependency
failures, all APIs, fenv restoration and no library allocation. `sanitize-scalar`
uses ASan/UBSan with LeakSanitizer disabled in this environment. Retained native
boundary/control/expression/uniform/compute/job/layout tests also remain useful.
Historical `test_api`, `test_constants` and `test_job.py` contain exact obsolete
rejection assertions for features intentionally lifted by M8; they are preserved,
not broadly relaxed. Raw `make test` therefore is not the M8 pass receipt. The
separate acceptance ledger must classify each lift and retain its first failure.

Independent frozen evaluator results remain distinct from developer tests.
Instruction-level CPU execution plus explicit `spirv-val --target-env vulkan1.1`
are not physical GPU, WebGPU conformance, backend/application adoption or speed
proof. The generic benchmark gate's EMIT-SKIP handling and unrelated prebuilt
validation are not all-source compiler coverage.

## Owner map

- lex.c / parse.c: one bounded grammar and original source positions.
- numeric.c / numeric.h: private typed numeric values, bounded literal arithmetic,
  scalar const operations/conversions and checked per-call host FP environment.
- validate.c: hoisted/module and lexical scope, dependency resolution, scalar
  stages/types, resource layout and typed control/value IR, including dead source.
- storage.c / internal.h: checked caller slices, private records and fixed adapters.
- spirv.c: typed declarations, safe scalar operations and structured robust/control
  lowering from IR identities, never source spelling.
- reflection.c: unchanged ABI1/v2 projections.
- compile.c: API preflight, diagnostics, fenv ownership and one publication point.
- cli.c / cli_json.c: caller allocations, bounded JSON, little-endian serialization
  and owned no-clobber publication.

See [local style](STYLE.md), [provenance](PROVENANCE.md),
[developer-test history](tests/README.md), and immutable milestone result documents
for earlier source-bound receipts. Compiler ownership remains incomplete until
broader language, target and real-consumer gates pass.
