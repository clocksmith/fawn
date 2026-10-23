# Independent M10 native evaluator: source freeze

This is a source-only, pre-candidate native evaluator. It is not an implementation
and currently establishes no compiler pass, native acceptance, GPU qualification,
performance result, production adoption, or completed migration.

The exact frozen public contract is commit
8530b04f6b2171bc24888530dd200e2f4a60983f. CONTRACT_10.sha256 has hash
a1051f4b1bcc6a0ce63130c744d969406f797db0f18c60b7fe2b00fa60c8503c;
all twelve entries were verified from that commit. Public headers here are exact
frozen copies. PREINSPECTION.json records immutable input identities and the six
accepted historical native files consulted at commit
dee5f42acbb50e05d5690f1d2d13777c5c26d51d. No historical sources, seals, receipts,
or candidate sources were modified. No current worktree implementation was read.

## Source ownership and independent expectations

- abi_m10.c: admitted flags 1/2/3/5/6/7 and invalid combinations; selected exact
  counts; measure/private-emission consistency; every output span independently
  short; zero/NULL discovery; success-prefix and all-output failure atomicity;
  padding, unused fields, names, same-spelling names not interned, dense nominal
  identity, NONE versus empty/zero distinctions, source/workspace destruction,
  and reuse. Includes 30 query and 47 compile-preflight branches, with conditional
  misalignment probes only where public alignment exceeds one.
- capacity_m10.c: fifteen explicitly settable logical pools; zero, independently
  located exact-success boundary, and one-short boundary in all six output
  modes; first exhausted pool and attempted lower bound; no false exact-count
  claim; three explicit depth limits; repeated calls without reset. The single
  broad fixture deliberately needs aggregate constants, edges, projection,
  runtime operands, references, declarations, and output staging.
- host_m10.c: four rounding modes, existing exception flags, deterministic
  outputs, errno, success/failure/shortage in all output modes, and eight
  independent concurrent workspaces. On GNU Linux x86, also checks complete raw
  fenv, MXCSR FTZ/DAZ, and disjoint exception traps. On other hosts only portable
  C observable fenv claims apply. The output reference is for host-state
  independence, not an independent arithmetic or SPIR-V semantic oracle.
- faults_m10.c: discovers actually reached fenv operations, injects failures at
  every observed ordinal before/after host side effects and persistent restoring
  operation failures, checks bounded cleanup, errno, unpublished bulk spans,
  one-shot observable environment restoration, and reuse. Unobserved operations
  are printed and never credited as injected coverage. GNU exception-mask APIs
  are wrapped only on glibc. Persistent restoration failures remain failed and
  unpublished; exact restoration is not asserted when the host forbids it.
- allocator_m10.c: archive-link allocator interception with a positive control
  proving malloc/free interception before measuring library calls. Both query and
  compile windows are covered. Static import review complements interception;
  shared-library interposition is not falsely credited from archive --wrap.
- legacy_m10.c: all five old exports, source-only consumers, old scalar success,
  new local aggregates with representable reflection, exact legacy-projection
  failure prefix and offending declaration, old output atomicity, reuse, and
  updated frozen fixed-workspace ceiling. This is not a replacement for the
  main evaluator's complete unchanged historical suites.
- consumer_m10.cpp: independently compiled C++17, fixed record sizes/offsets,
  standard-layout checks, no RTTI/exceptions, measure/compile and owned names.

Every fixture and assertion was authored before candidate inspection. No source
substring recognizer or candidate diagnostic was used to generate expectations.
The main evaluator separately owns WGSL byte layout, complete reflected-field
values, target instructions, arithmetic semantics, witnesses, historical corpus,
stack/frame/record audits, CLI I/O/allocator failures, and physical-device gates.

## Important qualification boundaries

Full-capacity spans and descriptor objects are pairwise disjoint. No overlap
rejection or overlap no-write promise is asserted. Misalignment probes use
addresses within real allocated objects, not fabricated invalid addresses.
Overflow descriptor tests use NULL output pointers, never pretend an inaccessible
SIZE_MAX span exists, and claim INPUT_ERROR only, not a particular validation
ordering. Source/descriptor lifetime and one active call per workspace remain
caller preconditions.

TYPE_SYNTAX is derived from tokens as floor(T/2), not independently settable in
ABI4. This native suite does not claim a separately reached TYPE_SYNTAX exhaustion
path. Its three public depth counters are tested; mutual parser SCC stack
accounting remains the main evaluator's explicit source audit. Query overflow
probes do not assert unavailable exact private layout byte formulas.

C11 sources consume only public headers. Threading uses POSIX pthreads; the
source-build orchestration currently requires ELF/binutils and a GCC-compatible
compiler/linker. Architecture-specific fenv claims are conditional and explicit.
Fresh archive and fresh shared-library consumers are both required by the runner.
No prebuilt candidate artifact or private evidence directory is required after
copying this sealed source directory and selecting a repository commit.

## Safe usage before evaluator handoff

python3 run_native_m10.py

This verifies the source seal only. It does not inspect or execute a candidate.

python3 run_native_m10.py --syntax-only --out /path/to/fresh/syntax-receipt

Only independent native sources are syntax-checked against frozen headers. No
objects, libraries, or executables are produced. Python is parsed using ast
without a .pyc file during preparation.

## Future execution, only after explicit handoff

python3 run_native_m10.py --evaluate-candidate --repository /path/to/repository --candidate FULL_40_HEX_COMMIT --out /path/to/fresh/native-receipt --build-dir /tmp/fresh-task-private-native-build

The runner requires a fixed commit, extracts a source snapshot with git show,
checks all four public headers against the freeze, records source identities,
compiles a fresh source archive and shared library, validates exactly seven
public exports and no direct allocator imports, then independently compiles and
runs the consumers. It preserves every failed command/output and refuses to
reuse an existing receipt directory. Optional --sanitize additionally builds
ASan/UBSan variants and records each outcome. Use a fresh task-private /tmp build directory for the source snapshot and sizable
intermediates while keeping compact receipts in the native evidence directory.
Inspect available disk before that future stage. No source-side deletion or automatic profile growth is performed.

The SOURCE_SEAL.sha256 file seals sources/provenance before candidate inspection;
SEAL_RECEIPT.json records its identity and preparation-only validation. Any future
evaluator repair must preserve the original seal and explicitly disclose scope,
timing, and candidate knowledge instead of rewriting this freeze.
