# Independent M8 scalar numeric evaluator

This compact source-only suite imports the unchanged accepted historical suite
from an explicitly supplied Doe checkout. It neither vendors compiler sources nor
requires an archived binary/evidence directory. GCC/G++14.2, Python3 and an
already installed spirv-val are the required host tools. Never invoke Zig for
this C compiler path. Every target validation explicitly selects Vulkan1.1.

Accepted prior owner:7478e5d071a1ea3745d6ad9725a62a29c04d84b3.
Frozen contract:0e1e201030c60474e07db99cdbb1a5ea025d3989.
Historical imports:runtime/c/compiler/independent_tests_m7, byte unchanged.
Normative WGSL:https://www.w3.org/TR/2026/CRD-WGSL-20261021/ .

## Reproduction

Pass absolute paths and a fresh evidence directory:

```
python3 run_acceptance_m8.py --repo /path/to/doe \
  --source-root /path/to/doe/runtime/c/compiler \
  --validator /installed/path/spirv-val --evidence /fresh/output
```

`--plan` prints commands without candidate execution. `--import-check` imports
all source modules using ordinary PYTHONPATH, no sys.path mutation. The front door
sets historical imports explicitly. `--stages scalar,constants` selects focused
stages only, never full acceptance. Build stages are named build_library/build_cli;
include them when not using `--existing-build /explicit/source-built/output`.
An optional `--prebuilt /explicit/library.so` adds a separately labeled native
consumer path; it never replaces source-build evidence. GCC library/CLI builds go
to the fresh evidence directory, never the candidate checkout. Host builds use
-fno-fast-math -ffp-contract=off -frounding-math. Native and CLI sanitizers use the
same pinned source, with ASan/UBSan and no downloaded dependencies.

Full stages: independent handwritten instruction controls; scalar/constants/six
unchanged witnesses; exact capacities; retained historical/lift cases;
post-disclosure shift supplement; actual emitted-module mutation controls;
same98 original consumer; native C/C++/fenv/allocation/concurrency/layout/frames;
three-mode CLI acquisition/rollback/environment failures. Output never conflates
EMIT-SKIP with admission or prebuilt modules with candidate emissions.

## Independence and corrections

FROZEN_OBLIGATIONS_M8.md and PREINSPECTION_SEMANTIC.sha256 document the initial
pre-candidate inventory. Exact source/expectation seals are summarized in
FREEZE_INDEX.json. Fixtures derive expected values using Python integers and
Fractions, not reflection or candidate constants. compute_oracle_m8 executes
actual SPIR-V instructions, branches/Phi predecessors and access guards. Unknown
instructions are limitations, never passes. Dangerous div/shift/convert operands
are checked before any later result-select. Runtime bit/subnormal results are
CPU-model evidence, not universal physical-device claims.

Original inventories and first results are preserved. EVALUATOR_ERRATA_M8.md
lists every narrow evaluator correction: two out-of-exact-finite-range positives
become required negatives; seven exact rejection status overconstraints remain
rejections; simultaneous constant/node budgets may report either named cause;
original word-pressure source remains a distinct atomic word-boundary check;
a separate mutable-bool source proves fixed IR exhaustion. It also preserves the
first invalid mutant construction. Historical files and source bytes are never
modified. The normal front door applies these documented corrections; direct
semantic/capacity scripts expose `--apply-errata`, so the original expectation
behavior remains reproducible. supplemental_m8.py labels all eight post-disclosure
shift cases separately. New emitted mutants are also postinspection controls.

## Candidate1 result

997 exact semantic cases resolve to826 positive/171 negative after the documented
correction, with2,999 independent CPU invocations. All839 immutable historical
cases pass, including67 named new positive lifts (21,528 invocations). Eight
post-disclosure overload cases and31 capacity checks pass. All36 handwritten
controls plus17 valid emitted mutations pass; mutations are detected by execution,
not by spirv-val rejection. The actual unchanged consumer emits23 of98 paths;
all23 target-validate and execute (11,626 invocations),75 remain real rejections.
36 prebuilts are observations only,35 validator-valid, not candidate acceptance.

Native:2368 caller-environment calls on each archive/shared/provided-prebuilt/
sanitized path,60 injected host failures and174 numeric CLI checks pass. Exact
headers/exports remain unchanged; no library allocations or publication before
restoration. Workspace76512 bytes, ABI1 frame76592, other frames<=1344. New broad
conservative aggregate stack estimates are35488/88960/22224 bytes for parser/
semantic/emitter, excluding adapters/callers; these are not measured high-water
marks or reductions from M7. Explicit frame/workspace caps and64/32,18/4 retained
depth/count bounds pass. See STORAGE_LEDGER_REVIEW_M8.md.

Candidate1 manifest:a877ba32e0328f0e98d9c6d62f982dc706b657d7a73c1280a5034b0b9961ebba.
No candidate implementation corrections were needed by this independent campaign.
No GPU execution, runtime adoption, performance, production default or entire-Doe
migration claim is made. MIT license; historical oracle attribution retained.
