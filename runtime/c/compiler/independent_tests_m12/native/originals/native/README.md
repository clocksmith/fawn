# Independent M12 native gate preparation

This is evaluator source-only preparation, not an acceptance result. It neither
reads nor builds a candidate unless the caller adds `--execute`. The original
931-case seal and M11/M10 harnesses/receipts remain unchanged. New scripts are
small wrappers over the accepted source build and retained native front doors.

## Immutable input binding

- Case/oracle seal: `98a6656e0b3bf2bb2a6e74bb5977afabdab2ac2ac07c664468a8026dea43b281`.
- Contract seal: `f3fef320194d5a81fa67d1cd394bd675c287e25b5ea492260758929bd1f274a4`.
- Selected accepted compiler baseline: `f01effdf070edf2f25cf0f2016c995df1e4d3bab`.
- M11 reusable native recipe identity remains pinned by its original
  `PORTABLE_NATIVE_INPUTS.json` and 219-file native seal. No preserved test or
  expected value is edited.
- `NATIVE_INPUTS.json` names 88 source cases selected solely from the frozen
  evaluator manifest, nine pool seeds, 32 host profiles and six output masks.
  New integration was prepared after author implementation began but before
  any candidate source inspection, build, invocation or observed result.

## Explicit staged front door

Run `python3 /tmp/doe-m12-native-20261023/run_native_m12.py` with all of:

```
--repo /workspace/shared/doe-c-migration-20261023
--cases /tmp/doe-m12-independent-20261023
--native /workspace/shared/doe-c-migration-evidence-20261023/milestone11/evaluator/native
--owner /ABSOLUTE/IMMUTABLE/CANDIDATE/runtime/c/compiler
--manifest /ABSOLUTE/IMMUTABLE/CANDIDATE/SOURCE.sha256
--manifest-sha256 EXACT_CANDIDATE_MANIFEST_SHA256
--build-root /tmp/NEW-m12-native-build
--out /tmp/NEW-m12-native-STAGE-receipts
--stage build|numeric|retained|audit|sanitize
```

Each stage needs a fresh receipt directory. Without `--execute`, this verifies
only frozen inputs and writes a plan. It does not read the candidate owner or
manifest, create a build directory, compile or run a binary. All paths are
explicit and code-bearing path characters are refused. Builds/outputs may not
be placed within immutable inputs.

After storage coordination and immutable candidate handoff, run `build` with
`--execute`. It calls the accepted `build_m11.py` through the source-identity-checked
`supervised_build_m12.py` launcher, which disables only the original receipt
class's per-command wall timeout. All builder source, flags, commands and
assertions remain unchanged. The new exact
manifest binding compiles every current non-CLI source unit under GCC14.2.0,
strict C11 FP flags, stack-usage and call-graph output. It builds one fresh
archive/shared/CLI set and verifies seven exports, allocation imports, four
frozen headers and source identity. Artifact names retain `m11` solely because
that build front door is reused unchanged; receipts bind M12 source explicitly.

Subsequent stages require `--build-receipt /tmp/BUILD-STAGE-receipts/build/BUILD_RESULT.json`
and the same build root, owner and source manifest. They verify every artifact
hash and owner input before use. All stages preserve first failure, command
logs and emitted artifacts; no automatic candidate changes, pool growth or
retry from revised expectations occurs.

- `numeric`: archive/shared routes of new native checks. Each of nine E/Q/K/L/I/O/D/V/W
  pools is isolated at the unchanged full typed profile, minimum capacity is
  bounded-searched, exact and one-short are tested twice per mode without
  workspace reset. Each call checks error category, first exhausted pool,
  lower bound, unpublished counts, all-output canaries, redzones, adverse FP
  state, errno and zero protected library allocations.
- `numeric` also runs 88 selected frozen sources through ABI1/v2/job/typed.
  Thirty-two rounding/exception/trap/FTZ/DAZ profiles check full fenv/MXCSR/errno
  restoration on success, chosen new semantic errors and output-shortage exits.
  Successful per-adapter bytes are compared only across caller host profiles,
  not against another adapter as a semantic oracle. Sixty selected positives have only scalar storage-array resources and no structs.
  Exactly four runtime i32 input arrays are independently mapped to named legacy
  reflection UNSUPPORTED while typed remains successful; scalar does not imply
  representable in the old u32/f32-only ABI. Unrelated aggregate projection is
  not assumed. Eight concurrent distinct typed workspaces exercise the selected
  vector clamp source and per-thread floating state.
- `retained`: exact M10 C/C++ ABI, capacity, lifecycle, host, fault and historical
  consumer recipes plus the exact corrected M11 boundary harness, host controls,
  public work-cap twin, and private N/N-1 work/adapters. T/S/R/X controls are
  included through unchanged `capacity_m10.c`. Original failed M11 boundary
  source is retained but not rerun in place of its accepted correction.
- `audit`: unchanged measured layout harness and complete .su/.ci parser,
  all frames, seven recursive SCCs, address-proved compiler aliases, all
  nonwrapper frame sum and fixed/job combined path ceilings. Only owner/build
  and source-manifest literals relocate in the preserved M11 ledger audit;
  expected sizes/formulas/ceilings never change. Includes target-ID saturation
  control and object/linked-ELF data audit, with no mutable compiler globals.
- `sanitize`: fresh ASan/UBSan source build includes every current source unit,
  then all new numeric/pool controls and original affected M10/M11 controls.
  Private work controls also use every current source unit, not the obsolete
  enumerated M11 source list. It is separately storage-coordinated, not implied
  by unsanitized results. No TSan claim.

## Supervision and unfinished gates

There is no automatic wall-clock timeout in the M12 runner or supervised source
build. Native harnesses emit line-buffered per-profile and per-pool progress.
The runner writes RUNNING.json at command start and every30seconds with PID,
current label, elapsed time, completed command count, latest output and log size.
These are supervision ticks, never a deadline or progress-based acceptance.
The parent can inspect the live process/log and explicitly stop a stalled gate.
An external signal, explicit interrupt, conventional timeout exit or retained
Python TimeoutExpired is recorded as INCOMPLETE_GATE/UNFINISHED_GATE, not a
compiler defect or pass. Partial artifacts and logs remain. Nonzero assertions
are FAIL_NATIVE_GATE pending diagnosis; they are not automatically attributed
to the candidate. Every command receipt and target manifest binds the exact
M12 source manifest even where the reused builder retains m11 artifact names.

## Independent target handoff

Each successful selected native source emits little-endian `api1.spv` through
`api4.spv` under its fresh route/case output directory. `TARGET_MANIFEST.json`
uses the parent consumer's {"targets": [...]} schema and names case ID, immutable
source/hash, fixed/v2/job/typed ABI, route, target path/hash and the exact
case/scenario key in the sealed cases.json. Parent consumes these with the
sealed independent model/oracle and the approved spirv-val:
`/workspace/shared/doe-language-experiment-20261023/phase2/deps/spirv-tools/usr/bin/spirv-val`
with `--target-env vulkan1.1`. All target semantic statuses remain explicitly
pending here. Binary ID/hash differences alone are not semantic discrepancies.

## Source-bound historical materialization

Do not create a second historical corpus. The already-materialized read-only
`/tmp/doe-publish-bootstrap-real35-1fjbtuzs/retained` has 787 verified files from
commit `dee5f42acbb50e05d5690f1d2d13777c5c26d51d` and PINNED_SOURCE.json digest
`0571ce4f474e3d34ae9cd15a6d0fa38d21d48fd5cf4ce7b4e08e599f677e8c1f`.
The canonical existing front door, if materialization is later truly necessary,
is `runtime/c/compiler/independent_tests_m10/postdisclosure/prepare_retained.py
--repo REPO --out NEW_ROOT`. This preparation performed no materialization and
no copy of historical corpora. Parent owns the exact98 and historical semantic
admission mappings; native recipes consume the existing hashed harness paths.

## Scope and remaining obligations

Source-only syntax/plan verification is not a native pass. No candidate build,
target execution, sanitizer run, stack/layout measurement or performance claim
has happened during preparation. Every call uses disjoint caller spans. Full
capacity nonoverlap is the inherited public precondition; this does not invent
an arbitrary-overlap detection guarantee. Original stack exclusions for external
libc/libm/OS/caller/sanitizer frames remain. This is not physical GPU/backend
numerical qualification, full migration or a production/default switch.
