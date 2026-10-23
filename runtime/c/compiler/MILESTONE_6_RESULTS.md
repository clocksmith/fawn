# Accepted M6: unified scalar contexts and compute inputs

The [frozen contract](MILESTONE_6.md) at
`b75ef8af6f7137bea46e90513e3bd85e25c6d53d` passes bounded independent
acceptance against retained M5 `14aa666`. Production selection is unchanged.

## Delivered owner and actual consumption

One parser and typed lowering now own scalar expressions in initializers,
storage indices and both comparison operands. The old separate atom/index
grammar is removed. Nested accesses independently return zero or discard a
store at each resource boundary; wrapping u32 arithmetic precedes each guard.
Source spans replace duplicated expression-name arrays without retaining token
pointers across scratch lifetimes.

Compute inputs resolve by semantic builtin/type, not argument order or name:
global/local invocation IDs, workgroup ID, number of workgroups and scalar local
index. Three-dimensional workgroup sizes have checked products. Zero-resource,
zero-parameter and empty-body entries are admitted without invented inputs.
The 320-node/18-depth bound preserves the older separate-index allowance; it is
not a permanent full-compiler architecture limit.

The unchanged emitter consumer now compiles 11 of the same 98 source paths,
containing nine distinct sources. All eleven actual output modules pass explicit
Vulkan 1.1 validation and independent instruction execution in 1,499 invocations.
New real sources include gather and copy-buffer kernels. Four other newly
admitted files are empty compute shaders, not texture/draw support despite their
names. The [ledger](../../../docs/c-migration/corpus-coverage-m6.json) retains
87 explicit failures: 71 lexical and 16 parser failures, with later stages not
reached. No gate-main EMIT-SKIP result is used as a coverage pass.

## Independent acceptance and defects

The [portable source-only suite](independent_tests_m6/README.md) was frozen before
candidate inspection. Its constituent focused stages pass:

- New semantics: 40 positives, 43 negatives and nine deliberate emitted-opcode
  defect controls, totaling 92 checks and 1,263 instruction invocations. These
  include 577 synthetic single-invocation uint32 boundary injections and 58
  coherent invocation-batch trials. Copy trials include whole dispatches;
  gather samples relevant/guarding lanes and the all-builtin trial covers one
  complete nonzero workgroup, not every group in its declared grid.
- Retained semantics: 2,582 checks, 19,040 instruction trials and 648 historical
  cases. Exactly 34 previous exclusions are lifted under the frozen grammar;
  malformed, type, access and scope failures remain rejected.
- Native C/C++ source/prebuilt consumption, ABI sizes/offsets, exact exports,
  no-library-allocation checks, guard pages, output atomicity, failed-workspace
  reuse and concurrent calls. Retained/new ASan/UBSan mutation checks pass.
- All three CLI modes, allocation and I/O failures, no-clobber publication and
  owned cleanup pass 626 independent checks. Current CLI/API modes emit equal
  words for the same admitted capability; old-revision target bytes change for
  270 storage positives, as permitted by this experimental contract. This is
  not a cache-identity or production-artifact promotion.

The developer found one actual regression before the independent snapshot:
lone `@` incorrectly changed from parse error to unsupported after zero-resource
admission. The entry attribute owner now requires a name token first. The
original failing retained log and successful repaired tests are preserved.
The independent evaluator found no subsequent implementation defect. Its first
native invocation omitted the explicit validator path and stopped before
candidate execution; the corrected invocation and original failure are retained.

The 41-file selected suite (39 payload files plus two manifests) reuses historical MIT oracles once,
with ordinary imports and no hidden baseline binary requirement. Historical
M1–M5 suites remain unchanged. All constituent campaigns ran; packaging uses
manifest verification, plan/import checks and a four-case semantic smoke, not
another claimed full-suite run.

## Measured native storage and cost

Linux x86-64, GCC/G++ 14.2.0, strict C11 `-O2`, existing installed tools:

- Caller workspace: 96,208 to 77,760 bytes, alignment eight. ABI1 static frame:
  96,272 to 77,824, with no heap substitution. Both public headers are unchanged.
- V2 entry/core frames: 64/80 bytes; largest other library frame: 2,016 bytes.
  Parser/lowering recursion and individual callee frames are recorded separately.
  CLI main remains 107,728 bytes. None is a measured combined stack high-water.
- CLI/shared-library sizes: 72,232/62,344 bytes, versus M5 72,424/62,536.
  Active implementation C/H totals 2,120 lines versus 2,016, still eleven files.
- Strict source builds and focused developer tests pass, including all six
  unchanged witnesses, native/API boundaries and ASan/UBSan. No matched build,
  execution-speed or lifetime maintenance comparison was measured in this
  increment; reduced workspace is not a general performance claim.

Exact commands, flags, source/artifact hashes, first failures and measurements
are retained under the isolated `milestone6` evidence directory. CLI SHA256:
`d87af66a0c7fb5a561b38f6a9d16d5b69f0103f79e8d342ed748057ee64e82c2`;
library SHA256:
`79bb627445c47d5b5be4eb4ab1a64c2d3cc4d99ac4699f66a69019cb8bc7ed6d`.

## Remaining boundary

This is useful compiler-owner expansion, not complete WGSL, runtime adoption or
physical GPU execution. TranslationInfo/cache/config identity, other targets,
full compiler callers and backend qualification remain open. No production Zig
path is replaced. The [remaining-owner roadmap](../../../docs/c-migration/COMPILER_ROADMAP.md)
selects coherent language/storage responsibilities rather than per-shader rules.
