# Accepted M5: flat uniform resources and caller workspace

The [frozen contract](MILESTONE_5.md) at `d618a225ef6bb4f8eb476dc33e05038ecaf2a3ac`
passes its bounded independent acceptance against retained M4 `410c181`.
No implementation defect or repair was found after the first candidate. This is
experimental compiler coverage, not production replacement or physical execution.

## Delivered owner

General flat u32/f32 structs, typed uniform member access, declaration-ordered
layout reflection and fixed Uniform-class SPIR-V accesses now share the existing
parser/validator/emitter. The unchanged package vector-scale/add source compiles
through the existing emitter consumer. Eight structs of up to eight members and
eight total resources remain explicit limits. The single module const retains
its leading position; type/resource references are declaration-order independent.

A separate v2 native API exposes caller-owned workspace and versioned reflection.
ABI1's header/layout/storage-only admission remain unchanged and allocation-free.
One lifetime-based scratch union avoids a second pipeline or hidden large v2
stack workspace. CLI binary-only uses v2; legacy reflection retains ABI1 and exact
old JSON; explicit --reflection-v2 publishes the new schema. Owned allocation,
no-clobber publication and ordinary rollback are exercised independently.

Uniform minimum extent includes every field: three scalar fields require 12
bytes, separately from conservative uniform aggregate alignment 16. Storage
runtime scalar arrays require minimum extent 4. Short uniform buffers are a host
admission failure, not compiler-generated zero-fill. Device descriptor offset,
usage, limits and lifetime remain runtime gates.

## Independent evidence

The [source-only suite](independent_tests_m5/README.md) was case-frozen before
candidate inspection. All of these constituent stages executed successfully:

- 32 new positives and 51 negatives: 83 checks, 735 actual emitted-instruction
  trials with independently packed uniform bytes, nine deliberate layout/type/
  member/resource/binding mutations detected.
- Retained M4: 1,891 checks and 17,625 instruction trials; 12 retained oracle
  mutation controls and 40,000 finite arithmetic reference cross-checks.
- All 270 retained positive source SPIR-V outputs match the preserved M4 ABI1
  artifact when compiled with current ABI2. Existing legacy JSON bytes match.
- Native C/C++ source/prebuilt consumers, size/offset checks, workspace query and
  admission, reuse after failure, guard pages, independent threads, no library
  allocation, exact three-symbol exports, ASan/UBSan and malformed-source trials.
  Each M5 native mutation run uses 12,000 mutations; retained M4 uses 50,000.
- 599 CLI checks cover all three modes, versioned JSON, query/allocation failure,
  ordinary and instrumented I/O faults, races, no-clobber and owned cleanup.
- Same 98-path / 91-content corpus through unchanged consumer calls: five paths
  / four unique sources admitted, explicitly validated as Vulkan 1.1, and the
  actual five consumer-emitted modules executed in 856 independent trials.
  Remaining first failures are 71 lexical and 22 parser; later stages were not
  reached. [The new ledger](../../../docs/c-migration/corpus-coverage-m5.json)
  separates these phases from static missing-feature inspection and backend gates.

The entire portable wrapper was validated with --plan, not rerun as another full
campaign. All underlying stages ran; its final bundle location passed 83 semantic
and 270 source-bound baseline checks. After copying the exact 41 selected files
into this repository, the lead verified their manifest, --plan and all 83 semantic
checks against the same prebuilt library. No binary/cache/nested historical suite
forest is included. The historical M1–M4 suites and results remain unchanged.

Initial evaluator mistakes are retained: a legal member-plus-literal condition,
invalid wider-workgroup/empty-body/group fixtures, and an incorrect result-size
expectation. They were corrected narrowly, without relaxing malformed/type-invalid
cases. A stack checker initially included CLI main in the native-core budget;
independent contract review confirmed the native scope. The original failure is
preserved and the CLI frame is reported below. No implementation fix or budget
waiver is implied.

## Native cost and ownership measurements

Linux x86-64, GCC 14.2.0, strict C11 -O2, installed tools only:

- Caller workspace: 96,208 bytes, alignment 8; v2 result: 6,904 bytes. ABI1 result
  remains 1,008 bytes, with its old enum/field layouts checked in C and C++.
- ABI1 static frame: 96,272 bytes, down from M4's 105,328. V2 wrapper/core:
  64/80 bytes. Largest other library frame: 1,984 bytes. Recursive parser frames
  are 912 bytes; expression lowering is 176 bytes with existing bounded depths.
- CLI main separately retains a 107,728-byte frame for source/words/binary/paths.
  These are per-function measurements, not combined high-water or universal
  stack qualification. V2 makes job storage explicit; it does not eliminate it.
- CLI/library artifact sizes: 72,424 / 62,536 bytes, versus M4 59,616 / 54,064.
  Active implementation C/H owners: 1,594 to 2,016 lines, eight to eleven files;
  reflection and JSON serialization are separated by responsibility.
- One local forced source build: 1.932 seconds; no-op build: 0.0022 seconds.
  These are local observations, not a matched performance/language comparison.

Developer strict builds, retained/new tests, ASan/UBSan, uniform/native clients
and instrumented CLI checks pass. LeakSanitizer is unavailable under ptrace;
explicit allocation/free ownership checks remain enabled. No tool installation.

## Identity and evidence locations

First, independent source-built and final developer artifacts are byte-identical:

- CLI SHA-256: `e3146ac66b35cb590499ae521be916b6b03b2b62118d8029e0504cb48f24bf6e`.
- Library SHA-256: `8f85dfdd614eebd118a3aad706c60ec91e1eb5e6e9219989b9cc82ac584df6a3`.
- Portable SOURCE.sha256 file SHA-256:
  `6dc3673c503920018370a6217060cc06916ba595ad7b05fc8b5460d324cab5db`.
- Phase ledger SHA-256:
  `e0f6d0277a6136b93fcb8d81cdca0ebf25198d21035c25a8acff74e0ccbe4678`.

Local receipts are under
`/workspace/shared/doe-c-migration-evidence-20261023/milestone5/`:
`evaluator/ACCEPTANCE.json` binds stages and first failures (SHA-256
`02c48f1908c38979707f710afb2ea6a408319da093cb4c195961d491a56f944d`);
`developer/developer-receipt.json`, `stack-usage.txt`, `layout-sizes.txt`,
`final-source-artifact-hashes.json` and `build-measurements.json` bind developer
commands/costs. `baseline/` preserves M4 artifacts and the pre-change witness
failure. Paths are local evidence locations, not published download links.

## Open migration gates

The other 93 corpus paths are not accepted, and prerequisite coverage in 69
uniform paths is not 69 executable kernels. No physical GPU, Vulkan device,
runtime/application adoption, full numerical envelope, TranslationInfo/cache/
entrypoint metadata or other target backend is qualified here. Zero production
owners have been replaced; the retained Zig baseline and original main remain
unchanged. Continue coherent compiler owners, not production switchover.
