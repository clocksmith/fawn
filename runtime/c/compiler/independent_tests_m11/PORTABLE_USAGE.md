# Portable M11 independent evaluator

Import this complete source/receipt tree as `runtime/c/compiler/independent_tests_m11`. The front doors are this file, `REPORT.md`, `VERDICT.json`, and the complete-file `IMPORT.sha256`. No compiler executable, object, library, emitted target, cache, or duplicated historical corpus is required in this package. `native/candidate2-GCC_STACK.tar.gz` contains only GCC `.ci`/`.su` text receipts. Original failed attempts and superseded expectations remain evidence, not current acceptance rules.

## Verify the completed result without repeating the expensive test

From this directory, run `sha256sum -c IMPORT.sha256`, then `python3 verify_import.py`. This only checks the preserved source/receipt package. It does not execute a compiler or target and does not turn historical observations into a fresh host result. The original full command, artifact identity, terminal exit, supervision bounds and lack of a wall-clock timeout are in `RUN_INVOCATION.json`. Final source/contract identities are in `FINAL_SOURCE_CONTRACT_RECHECK.json`. Final source-built and handed-prebuilt observations remain separate.

The long unchanged witness alone took 5084.373908 seconds in this evaluator. Do not rerun it merely to import this accepted evidence. Full acceptance was 120 effective cases, 24 execution scenarios, four schedules and two poisoned dispatches each. Same98 is 52 emitted, 52 validated, 43 executed and 46 rejected, with nine explicitly validation-only emissions. A partial selection or validator-only replay is not the full gate.

## Fresh replay inputs

Use existing Python3, GCC/G++14, binutils, pthread/libm, and the approved SPIR-V validator. This evidence qualifies GCC14.2/Linux x86-64, not arbitrary host/toolchain behavior. No install, network fetch, Zig, GPU, or runtime adoption is part of this route. All build/output directories must be NEW paths outside immutable source/evidence trees, preferably a task-private `/tmp` directory. Set `PYTHONDONTWRITEBYTECODE=1`.

Define explicit absolute paths:

- `E`: this `independent_tests_m11` directory.
- `R`: Doe repository containing the existing accepted `independent_tests_m10` and historical inputs.
- `M10=$R/runtime/c/compiler/independent_tests_m10`.
- `OWNER`: extracted immutable candidate `runtime/c/compiler`, not an assumed moving checkout.
- `MANIFEST`: original candidate `SOURCE.sha256`, digest `b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af`.
- `VAL`: approved `spirv-val` executable. Each ordinary validation uses `--target-env vulkan1.1`.
- `B`, `W`: new build and receipt roots. The children named below must not already exist.

The accepted source archive digest is `c3ddb0594195aff3d157f7ee5cc488c4df7289f6c91f6b2dbe49c0302ba6172a`. The package stores source/header/contract manifests, not candidate source or binaries. Verify archive and source identity before candidate execution.

## Native build and boundary replay

See `native/PORTABLE_NATIVE_USAGE.md`. The path-explicit front door is:

```sh
python3 "$E/native/portable_native_replay.py" \
  --repo "$R" --owner "$OWNER" --manifest "$MANIFEST" \
  --build-root "$B" --out "$W/native"
```

The default verifies identities and prepares a plan only. `--execute` performs the selected fresh route. Optional `--retained`, `--sanitize`, and explicit `--prebuilt PATH --prebuilt-sha256 HASH` add their separately attributed routes. Original native acceptance already covered these routes; the new portable orchestration itself received only syntax/identity/plan checks, not a duplicate full run. It preserves the original known-bad boundary harness and runs the exact corrected revision instead.

For a build-only replay, `native/build_m11.py` accepts `--evaluate-candidate --repo "$R" --owner "$OWNER" --manifest "$MANIFEST" --expected-manifest-sha256 b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af --build-root "$B" --out "$W/build"`. Do not run both builders into the same destination.

Frozen ledger: `$R/runtime/c/compiler/MILESTONE_11_LEDGER.md`. Audit source: `native/audit_layout_stack_m11.py`; original result: `native/candidate2-LEDGER_STACK.json`. The portable native wrapper relocates the audit's historical absolute paths into a new receipt copy and records both hashes, without changing arithmetic or expectations.

## Independent target execution

After a fresh build, run the unchanged full matrix and both small-fixture granularity selections:

```sh
PYTHONPATH="$E/oracle" python3 "$E/run_cases.py" \
  --library "$B/libdoe-m11.so" --validator "$VAL" --out "$W/cases"
PYTHONPATH="$E/oracle" python3 "$E/granularity_crosscheck.py" \
  --library "$B/libdoe-m11.so" --validator "$VAL" --select execute_ --out "$W/event64-execute_"
PYTHONPATH="$E/oracle" python3 "$E/granularity_crosscheck.py" \
  --library "$B/libdoe-m11.so" --validator "$VAL" --select control_ --out "$W/event64-control_"
```

Do not reduce schedules, iterations, groups, poison repeats, invocation counts or step/event bounds. The matrix checks expected bytes; cross-check receipts must additionally match target hashes, instruction totals and barrier counts, as the original `finalize_results.py` demonstrates.

For self-control replay, `oracle/self_controls.py`, `phase_controls.py`, `memory_controls.py`, `zero_controls.py`, and `skip_control.py` accept `--validator "$VAL" --receipt NEW_JSON`. `oracle/quantum_controls.py` accepts `--validator "$VAL" --out NEW_DIRECTORY`. `oracle/graph_controls.py` writes a script-relative receipt, so run it only from a complete new copy of this evaluator, never overwrite the imported evidence. Validator-invalid controls and validator-valid semantic controls must retain their distinct expected outcomes.

## Retained semantic cases and exact98

Accepted historical sources are referenced, not duplicated. Existing M10 `postdisclosure/prepare_retained.py --repo "$R" --out "$W/retained"` materializes and verifies the locally available commit `dee5f42acbb50e05d5690f1d2d13777c5c26d51d` against `historical/PINNED_SOURCE.json`; no fetch is needed or implied. Set `OLD=$W/retained` after this completes. Keep original accepted M10 manifests and adapters unchanged.

```sh
PYTHONPATH="$E/oracle:$M10/source" python3 "$E/run_m10_regression_contract_overlay.py" \
  --m10-root "$M10" --library "$B/libdoe-m11.so" --validator "$VAL" --out "$W/m10-regression"
PYTHONPATH="$M10/postdisclosure:$M10/source:$OLD/runtime/c/compiler/independent_tests_m9/source:$OLD/runtime/c/compiler/independent_tests_m8:$OLD/runtime/c/compiler/independent_tests_m7" \
python3 "$E/run_same98.py" --repo "$R" --retained-root "$OLD" \
  --cli "$B/doe-compiler-m11" --validator "$VAL" --out "$W/same98" --m11-results "$W/cases/results.json"
```

The default semantic overlay runs all 576 cases. It preserves accepted-M10 adapters, the presealed exact workgroup admission and only the separately documented exact private-source status mapping. `run_m10_regression.py` is the original M11 runner and intentionally retains its first private-status failure. The portable overlay received only the exact private-case check; original full historical results and correction controls establish the reported acceptance.

The exact98 runner checks frozen source hashes and the unchanged consumer hash, independently executes the 37 retained witnesses, and binds the six mandatory new witnesses to completed exact-target proofs from the full matrix. Nine other new emissions remain validation-only. `finalize_results.py` is the preserved original historical collator with original temporary paths and specific pending-long reconciliation; it is not a path-portable fresh-run command. Do not rewrite its inputs or imported original receipts to produce a new result. Use fresh replay receipts and preserve any difference explicitly.

## Seals and limitations

`CASE_SEAL.sha256` and `PRE_CANDIDATE_COMPLETE_V2.sha256` verify the original pre-candidate commitment and final pre-observation preparation. Earlier preparation manifests are preserved historically and may reference a subsequently corrected file; do not relabel them as current-path verification. Original copies and exact old/new mappings remain in this tree. The original 219-file `native/NATIVE_ARTIFACTS.sha256` and separate `native/PORTABLE_NATIVE_FILES.sha256` both verify independently. `IMPORT.sha256` binds the complete final import, excluding only itself.

No GPU, runtime metadata integration, physical-device behavior, performance parity, alternate-host ABI, TSan proof, or whole-Doe migration is claimed. Full-capacity nonoverlap remains the caller precondition. External/system/caller stack frames are outside the native C-owned bounds.
