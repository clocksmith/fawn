# Portable M12 native acceptance sources

This is a source-only replay package, not a new acceptance run. Original reviewed
native sources, seals, the exact one-source correction and normal-profile audit
recipe are preserved byte-for-byte under `originals/`. The top-level front door
only verifies inputs, records explicit path relocation, and selects a single
original stage. It does not edit compiler source, flags, expected values or
historical receipts. Default invocation performs no compilation or native test.

## Dependencies and provenance

- Linux x86-64 with qualified GCC/G++ 14.2.0, Python 3.10+, GNU binutils (`ar`,
  `nm`, `objdump`, `readelf`), glibc fenv/MXCSR facilities, pthreads and libm.
  ASan/UBSan must be available from that compiler for the separately released
  `sanitize` stage. No downloads, installs or network access occur in this runner.
- `--repo` is a repository tree containing the accepted historical native helpers.
  `DEPENDENCIES.json` binds 399 exact historical files to accepted commit
  `f01effdf070edf2f25cf0f2016c995df1e4d3bab`. Every listed byte was independently
  checked against its accepted git blob while preparing this package.
  The M11 helpers are read directly from
  `runtime/c/compiler/independent_tests_m11/native`; the 17 work sources are read
  from its sibling `frozen` directory. No historical corpus/helper is copied here.
  The original M11 all-artifact seal also covers its accepted historical
  `candidate2-GCC_STACK.tar.gz`: that existing repository blob is verified only
  for original-seal integrity, never extracted or used as current observations.
  It is deliberately not included in this package.
- Frozen `CONTRACT_12.sha256` is bound to contract-only commit
  `da9b75a09e93248f52749d5aa7dd414f4d8a0016`, SHA256
  `f3fef320194d5a81fa67d1cd394bd675c287e25b5ea492260758929bd1f274a4`.
- `--cases` is the separately packaged unchanged M12 case/oracle root, containing
  `CASE_SEAL.sha256`, `cases.json`, `GATE_PLAN.json` and the original relative
  source paths. Primary seal SHA256 is
  `98a6656e0b3bf2bb2a6e74bb5977afabdab2ac2ac07c664468a8026dea43b281`.
  Those 931 source cases and target model are not duplicated in this native portion.
- `--owner` and `--manifest` identify the exact compiler snapshot to evaluate.
  All manifest bytes, the source inventory and four frozen public headers are
  checked before the selected stage, and by the original stage after execution.
  Accepted M12 candidate2 manifest SHA256 is
  `73b51c7355912608f71f3f19c44b7f57f17ef3f8b05be9fbf05bccd5331aaaf4`.
- `--validator` must name the separately supplied approved `spirv-val`, SHA256
  `f386e67676bc134802b6bfec6d96fff0f9437d0f60e4c97ee0afa59ab4f1c4ea`.
  Only `--target-env vulkan1.1` is accepted. Native stages do not invoke it:
  their per-ABI `TARGET_MANIFEST.json` is handed to the separate parent-owned
  semantic consumer, which validates and compares complete output buffers.
  Validator/toolchain executables are external dependencies, never packaged.

No historical dependency is missing from the accepted repository identity.
M12 own sources/proofs are included; the M12 primary case package, candidate
snapshot, contract commit and external tools remain explicit separate inputs.
The M10 `prepare_retained.py` full-history front door remains source-bound in the
repository for the parent semantic/exact98 replay. These native stages do not
materialize another retained corpus or need a prior `/tmp` retained directory.

## Commands

Set absolute paths appropriate to the host. None of these values is inferred
from a previous `/tmp` layout:

```sh
PKG=/absolute/path/to/this/native-package
REPO=/absolute/path/to/doe-repository
CASES=/absolute/path/to/frozen-m12-case-package
OWNER=/absolute/path/to/immutable-candidate/runtime/c/compiler
MANIFEST=/absolute/path/to/immutable-candidate/SOURCE.sha256
MANIFEST_SHA=73b51c7355912608f71f3f19c44b7f57f17ef3f8b05be9fbf05bccd5331aaaf4
VALIDATOR=/absolute/path/to/approved/spirv-val
VALIDATOR_SHA=f386e67676bc134802b6bfec6d96fff0f9437d0f60e4c97ee0afa59ab4f1c4ea
BUILD=/absolute/path/to/new-native-build
OUT=/absolute/path/to/new-stage-receipt

# Verification only. Creates PLAN.json, PATH_VERIFICATION.json and any
# path-relocated source recipe, but invokes no compiler, executable or validator.
PYTHONDONTWRITEBYTECODE=1 python3 "$PKG/replay_native.py" \
  --repo "$REPO" --cases "$CASES" --owner "$OWNER" \
  --manifest "$MANIFEST" --manifest-sha256 "$MANIFEST_SHA" \
  --build-root "$BUILD" --out "$OUT" --stage build \
  --validator "$VALIDATOR" --validator-sha256 "$VALIDATOR_SHA" \
  --target-env vulkan1.1
```

Only after explicit resource coordination, use a **new** `--out` and add
`--execute` to execute one stage. The dry-run output directory is immutable
preparation evidence and cannot be reused for execution.

1. `--stage build`: new `--build-root`. Fresh strict source compilation, .su/.ci,
   archive/shared/CLI, seven exports, allocator-import and header checks.
   Build receipt is `BUILD_STAGE_OUT/gate/build/BUILD_RESULT.json`.
2. `--stage audit`: same build root, explicit
   `--build-receipt BUILD_STAGE_OUT/gate/build/BUILD_RESULT.json`.
   Exact layout, complete graph/SCC/path ledger, target-ID saturation and
   readonly-global/RELRO audit.
3. `--stage retained`: same build identity/receipt. Original M10/M11 native,
   historical ABI/capacity/fault/lifecycle/C++ and private work controls.
4. `--stage numeric`: same build identity/receipt. Original sealed 88-case native
   selection and nine pool seeds, 32 host profiles, six output masks, all four
   adapters and eight concurrent caller workspaces. Applies exactly the approved
   one-ID correction described below, not a broad expected-result rewrite.
5. `--stage normal`: **different new** build root, no build receipt.
   All 16 source units compiled using exact immutable Makefile CFLAGS, non-PIC,
   adding only .su/.ci measurement. Complete original layout/graph/SCC/path
   audit, kept distinct from strict-PIC results.
6. `--stage sanitize`: strict build identity/receipt, new output directory.
   Fresh ASan/UBSan compilation and all new native plus retained affected tests.
   Separate heavy-slot coordination is required. No TSan claim.

Run these serially; never overlap GCC builds or long/heavy stages. `--execute`
is intentionally not a request to execute another stage automatically.
All output paths must be outside package, repository, cases and owner roots;
build and receipt roots must be separate. Rejecting reused output paths keeps
original failures and every partial target output intact.

For steps 2, 3, 4 and 6, append this argument to the example command:

```sh
--build-receipt /absolute/path/to/build-stage-receipt/gate/build/BUILD_RESULT.json
```

Native and sanitizer target manifests are located at
`STAGE_OUT/gate/TARGET_MANIFEST.json`. They contain absolute generated output
paths, case ID/source/hash, ABI label, source-manifest hash and the frozen
scenario key. Pass them to the separate portable M12 semantic runner with the
same approved validator and target environment. Binary equality or ID/hash
inequality is not a shader semantic oracle.

## Exact relocation and correction behavior

The original general native runner already takes explicit paths; loading it
from its packaged original directory preserves its exact source seal.

For numeric/sanitize only, the front door verifies all original correction
proof hashes and creates a separate `relocated/CORRECTION.json` changing only
five provenance/input paths to the packaged copies. The original correction
wrapper is copied with only its one embedded correction-JSON digest updated.
Every original/relocated digest and path change is in `PLAN.json`. The only
semantic mapping remains `ai_explicit_f32_conversion`, exact source SHA256
`02266cfd91fcae1e15977229f544c06506d29a6d7e7e929322787ebe03acc243`,
typed and legacy status 0 to 4. Dated WGSL scalar conversion ranking and accepted
M11 scalar adjudication support it; the original failed native receipt is kept
as a small unedited JSON proof. No original source, seal or first result changes.

For normal profile only, a separate recipe copy replaces exactly one original
runner path literal. The normal source recipe and its original seal stay
unchanged. All stack ceilings and compiler flags remain exact original values.
Old absolute paths retained inside original source and receipts are provenance
or explicitly replaced old-path mapping keys, never implicit runtime inputs.

The original supervised runner has no automatic wall deadline. It emits per-pool
and per-profile progress and records current PID/command/counters. Interruption
is unfinished, not a compiler defect or passing result. External caller/libc/OS
and sanitizer frames are excluded from the qualified C-owned stack ledger.

## Inventory restriction

Only C/header/Python sources, Markdown, SHA256 seals and compact JSON are shipped.
No `.o`, `.so`, `.a`, native executables, `.spv`, observation archives or historic
corpora are included. `INVENTORY.json` and `SOURCE_SEAL.sha256` enumerate the
source package. Packaging verification is path/identity/AST-only and does not
re-run any passed heavy acceptance gate.
