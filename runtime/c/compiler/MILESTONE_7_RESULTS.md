# Accepted M7: configurable jobs and structured function state

The [frozen contract](MILESTONE_7.md) at `d377c075`, with the two append-only
empty-module-declaration amendments at `ea89534` and `c13b358`, passes independent
acceptance. Candidate1 required no implementation change during that evaluation.
Production compiler selection is unchanged.

## Delivered responsibility and actual consumers

One lexer/parser, semantic lowerer and SPIR-V emitter now support mutable scalar
locals, declaration-point initialization, lexical scopes, structured iteration,
continuing/break/continue/return and bool short circuit. Immutable locals remain
evaluated snapshots. Loops emit actual structured control flow and Function
storage, not host computation or source-specific unrolling. Dead source remains
validated; static loop behavior and dynamic harness termination are distinct.

The separately versioned job API owns checked caller-sized slices for tokens,
syntax, IR, scopes, values and output staging. It allocates nothing and retains
no caller pointers. Legacy ABI layouts and real fixed storage/reflection limits
remain, using the same pipeline. Experimental old rejection lists are not a
permanent language-compatibility promise: generic empty module declarations now
work across all APIs. Historical receipts and initial failures are unchanged.

All five unchanged matmul/dot-product witnesses and the separately identified
write-known-logits admission pass job API, binary-only CLI and reflection-v2
routes: exact expected reflection, matching words, explicit Vulkan 1.1 validation
and 26,883 independent CPU instruction invocations across the three routes.
The unchanged emitter consumer admits 17 of the same 98 paths, containing 15
unique sources, with 10,460 instruction invocations. The
[phase-separated ledger](../../../docs/c-migration/corpus-coverage-m7.json)
records 81 explicit rejections: 61 lexical and 20 parser failures. These are
first observed failures, not an assertion that fixing one token completes a source.

The separate prebuilt audit validates 35 of 36 existing modules. The retained
`draw_call_proxy_fragment.spv` uses BuiltIn Position in a Fragment entry and fails
Vulkan validation. It was not produced by this candidate and is neither hidden
nor counted against fresh-source compiler acceptance. EMIT-SKIP never counts as
source acceptance.

## Independent acceptance and failures retained

- Fifty generic positive and 58 negative cases, 97 finite instruction invocations
  and two deliberately unfinished dynamic loops. Nine opcode mutation controls
  are detected; an additional valid-module loop-order mutation is detected by
  actual instruction execution, not merely validator rejection.
- Retained M6 checks pass: 92 checks and 1,263 instruction invocations. The
  historical M1–M5 campaign retains its first four failures from two newly valid
  module-semicolon cases; exact amended positive rechecks pass in both old APIs,
  while real 2,049-token over-budget sources still reject. The independently
  recorded correction table distinguishes intentional admissions from defects.
- Native C/C++ source and prebuilt consumers, exact layouts/exports, no-library-
  allocator checks, guard pages, output atomicity, failed-workspace reuse,
  concurrent calls and 40 old fixed-boundary records pass. Configurations larger
  than a previously allocated workspace reject before accessing it.
- CLI allocation/I/O/no-clobber/rollback checks pass 414 checks, including
  ASan/UBSan. Native job and retained native consumers also pass sanitizers.
  LeakSanitizer remains disabled in this environment, not silently qualified.
- Actual depth-32 loop/continuing plus depth-64 expression, independent one-beyond
  rejections and 4,096 shallow sibling statements pass. Capacities do not increase
  C recursion limits.

Before the independent snapshot, target validation caught a real developer
defect: a predecessorless continuing block still needs SPIR-V's structural
back-edge. The emitter was repaired without executing unreachable source.
The first witness failure on valid `};` syntax caused the documented generic
grammar amendments, not a filename exception. Other retained developer failures
and their exact grammar-lift mappings remain in the evidence.

The independent evaluator found no further candidate defect. Its initial
negative-status assertions and untouched-workspace assertion exceeded the frozen
contract in named cases; original tests/receipts and narrow corrected rechecks
are preserved. They are not implementation fixes or relaxed malformed/type tests.

## Measured storage and artifacts

Linux x86-64, GCC/G++ 14.2.0, strict C11 `-O2`:

- Fixed caller workspace: 77,760 to 73,400 bytes, alignment eight; ABI1 frame:
  77,824 to 73,488 bytes. Largest other library frame: 672 bytes.
- The configured CLI job uses 1,995,208 caller-owned bytes, alignment 16. Scalable
  storage moves data out of recursive frames; it does not eliminate memory cost.
- Source-derived recursive-core bounds are 39,776 bytes for parser error paths,
  52,896 for lowering and 18,624 for emission. These are separate conservative
  core bounds, not summed process stack high-water measurements. CLI main is
  separately measured at 75,040 bytes.
- Independent CLI/shared-library sizes: 97,632/87,752 bytes. Active private
  implementation C/H: 2,967 lines, versus 2,120 at M6. This is expanded capability,
  not a matched language/performance or maintenance comparison.

Candidate source manifest SHA256:
`1d5cd9b9dfdc2821bf30a9775f8e57a7a093fd731b9d472d1f8537bf459d4ac3`.
CLI SHA256: `fb50a5fc9362727fa43f29f507dbbbdbc5b65101bdf8fa0687ebd7979cc459fa`.
Library SHA256: `570817de6eda59965239a7f5440ba959d2bd75bcbca310443a039a8de8f4551e`.
Commands, first failures, exact source identities and constituent results are in
the isolated `milestone7` and `milestone7-implementation` evidence directories.
The [portable source-only suite](independent_tests_m7/README.md) reproduces the
declared checks with an explicitly supplied installed validator; no historical
binary, tool download or private evidence directory is required.
Final-location manifest/import checks, a four-source/seven-invocation semantic
smoke and public-header/prebuilt C++ smoke pass without repeating the campaign.
Commit preparation removes one otherwise empty final line from developer
`tests/job_machine.py`; the evaluated source snapshot and both hashes remain
preserved. No implementation or independent oracle bytes change.

## Remaining boundary

This is neither complete WGSL nor runtime adoption. General numeric/type/module
semantics, aggregates, collectives, additional targets, TranslationInfo/cache
identity and complete compiler callers remain open. No physical GPU or whole-Doe
qualification follows. The migration continues through coherent owners and real
consumers; zero production-default Zig owners have been replaced.
