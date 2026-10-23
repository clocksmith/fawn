# Independent C compiler milestone 4 acceptance

This source-only evaluator freezes M4 scalar expressions, bounded exact decimal
materialization and immutable scoped locals independently of the compiler owners.
It retains M1/M2 behavior and M3 CLI consumption. No compiler implementation,
Zig, GPU simulation-as-qualification, downloads or installation is embedded.

Run from this directory with installed GCC/G++ and an explicit existing validator:

    PYTHONDONTWRITEBYTECODE=1 python3 run_acceptance_m4.py \
      --repo-root /path/to/doe \
      --compiler-root /path/to/doe/runtime/c/compiler \
      --validator /path/to/approved/spirv-val \
      --out /path/to/new-evidence-directory

The output directory must not exist. The runner snapshots only compiler owners,
source-builds CLI/library, then runs API/semantics, ordinary C/C++ consumption,
guard pages, concurrent calls, ASan/UBSan mutations/corpus, both CLI modes and
injected ordinary/sanitized I/O faults, executable oracle mutations and a
unique-output audit of the same 98 source paths. It never invokes gate main.
Use --dry-run to validate frozen inputs and inspect the exact command graph.
No Python import-path mutation or nested retained-suite copies are used.

M4 builds explicitly include numeric.c. Historical M2/M3 runners hard-coded the
older source list; their build command is not the M4 build command. The acceptance
here is source-built Linux GCC evidence. Prior accepted prebuilt artifacts remain
preserved separately; a source-built pass does not claim those binaries were tested.

FROZEN_CASES.md was written before implementation inspection. m4_fixtures.LIFTS
contains exactly 14 formerly rejected sources now explicitly admitted by frozen
M4 clauses. Every lift runs the unchanged source with independent expected buffer
effects, not merely successful compilation. All other 226 retained negatives and
69 new negatives reject; all 164 retained positives and 106 M4 positives execute.
No type/access/scope negative is generally disabled. Exact source limits, 64-node,
16-depth/statement, four-if, and leading-alias accounting stay enforced.

rational_oracle.py uses integers/Fraction for decimal-to-binary32 RNE and new
finite-operation references. typed_oracle.py executes actual SPIR-V instructions,
including multiply, subtraction, compare/select max and locals' SSA values.
Mutation controls independently falsify arithmetic opcodes, comparison/select
polarity, decimal ties, snapshots, branch edges, Phi values and robustness guards.
Vulkan1.1 validation is separate from instruction execution. Retained M1 addition
still exercises its broader historical numerical cases; new M4 numerical scope is
finite normals/zero, not NaN/inf/subnormal or physical-device qualification.

The first evaluator run used reserved WGSL word "target" in one renamed fixture.
That compiler rejection was correct. The original failed receipt remains; only
that fixture was changed to "sink_values" and rechecked. No implementation repair
or admission relaxation was needed. The supplemental suite is explicitly additive
post-black-box review, including 320 exact-rational decimal probes.

Only available host locales are tested, alongside all four glibc rounding modes.
This host provided C, C.utf8 and POSIX, not a decimal-comma locale. No locale was
installed. LeakSanitizer is explicitly disabled; ASan/UBSan are enabled. Compiler
static frame growth and bounded recursive frame sizes are recorded, not represented
as a peak-stack measurement or a performance improvement.

The 98-path ledger distinguishes lexical/parsing/semantic/emission/external
validation/instruction execution/backend stages and preserves actual first
failures. Successful source families do not establish runtime/application adoption,
complete WGSL coverage, physical hardware correctness, performance or full migration.
