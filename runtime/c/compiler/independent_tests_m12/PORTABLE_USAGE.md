# M12 independent evaluator: source-only portable front door

Accepted source manifest: `73b51c7355912608f71f3f19c44b7f57f17ef3f8b05be9fbf05bccd5331aaaf4`.
Contract seal: `f3fef320194d5a81fa67d1cd394bd675c287e25b5ea492260758929bd1f274a4`.
Primary pre-candidate seal: `98a6656e0b3bf2bb2a6e74bb5977afabdab2ac2ac07c664468a8026dea43b281`.
`receipts/ACCEPTANCE.json` is the concise source-bound verdict. `receipts/FIRST_FAILURES.json` and the immutable exact correction records preserve evaluator errors; candidate1's actual measured stack-budget failure is retained separately. Nothing here treats an old failed expectation as a compiler defect or broadens a frozen negative.

## Verify/import without rerunning acceptance

Run `sha256sum -c IMPORT.sha256` from this directory, then:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 portable_eval.py --repo /ABSOLUTE/DOE --stage verify
```

This validates packaged source/seals and source-bound dependencies only. It does not load a candidate, build, invoke a shader or change the repository. Original accepted M10/M11 sources and historical recipes come from the repository; no old corpus is duplicated. The portable orchestration received identity/syntax/plan checks only, not a repeated heavy acceptance run.

This package contains Python/C/header/WGSL/Markdown/JSON/hash text only. Emitted SPIR-V, native executables, object files, libraries and observation archives remain external local evidence. Original temporary paths in immutable receipts are provenance, not mandatory replay locations. The front door relocates repository references explicitly and records the sole same98 metadata-path rewrite. It never edits original sealed bytes.

## Explicit fresh replay paths and dependencies

Use existing Python3 standard library, qualified GCC/G++14.2/Linux x86-64, binutils, pthread/libm and the approved installed `spirv-val` (`--target-env vulkan1.1`). No install/network/GPU/Zig step is implied. Native source-only package has its own `native/README.md` and `native/replay_native.py`; build once into a fresh external `B`, then use that build's artifact hashes. The legacy `m11` artifact basenames identify reused build tooling only, not source provenance.

- `E`: this evaluator package.
- `R`: repository containing accepted `runtime/c/compiler/independent_tests_m10` and `independent_tests_m11` plus unchanged benchmark inputs.
- `OWNER`: immutable candidate `runtime/c/compiler` source directory.
- `MANIFEST`: its root-relative `SOURCE.sha256` file; `MH` is the verified digest above or an explicitly supplied later-candidate digest.
- `LIB`, `LH`: fresh independent shared library and exact SHA256.
- `CLI`, `CH`: fresh independent CLI and exact SHA256.
- `VAL`: approved existing validator; `W` is a new output root outside source/evidence trees.

Every execution requires explicit `--execute`, candidate owner/manifest/digest and artifact hashes. Without `--execute`, each stage only verifies inputs and prints a plan. No stage performs automatic downloads, builds, source fixes or capacity retries.

```sh
python3 "$E/portable_eval.py" --repo "$R" --stage primary --execute \
  --owner "$OWNER" --manifest "$MANIFEST" --manifest-sha256 "$MH" \
  --library "$LIB" --library-sha256 "$LH" --validator "$VAL" --out "$W/primary"
```

Run the same command with stages `numeric-supplement`, `corrections`, `historical576`, `retained120`, `lowering-ledger`, and `witnesses`, always using fresh output children. `primary` and `numeric-supplement` apply only the two already-adjudicated exact source/hash status corrections, retaining original manifests unchanged. `corrections` separately executes the valid wider-conversion/nonfinite-runtime controls.

`witnesses` executes all five complete unchanged sources, four schedules and two poisoned repeats: 40 dispatches, with original instruction/event bounds and observable counters. Do not replace this with reduced dimensions, fewer reductions or a guessed timeout. `retained120` freshly compiles/validates all120 sources; it carries prior execution only after exact target identity, otherwise it runs the original full schedules. A changed historical target in `same98` remains an explicit unfinished/failed gate until the corresponding source-bound original oracle is run; validation is not execution.

For exact98 after completed witness execution:

```sh
python3 "$E/portable_eval.py" --repo "$R" --stage same98 --execute \
  --owner "$OWNER" --manifest "$MANIFEST" --manifest-sha256 "$MH" \
  --cli "$CLI" --cli-sha256 "$CH" --validator "$VAL" \
  --witness-results "$W/witnesses/results.json" --out "$W/same98"
```

For `parser`, also supply `--baseline /ABSOLUTE/ACCEPTED_M11_LIBRARY --baseline-sha256 HASH`, independently source-bound to accepted M11. The qualification used baseline hash `5d301e813223c9602f7c7ad57634b0ad53a68ba8b752066fbfdce1e9f29b2553`. This is a control, not a replacement for fixed parser/status expectations. The exact type-depth one-short mapping preserves typed INPUT_ERROR1, pool19 and minimumN.

For independent native/sanitized output consumption, use stage `abi-targets` with `--target-manifest /ABSOLUTE/NEW_NATIVE/TARGET_MANIFEST.json`, candidate owner/manifest/digest, validator and new output. It validates and executes every emitted per-ABI artifact against fixed oracles, not merely across-adapter equality.

## Accepted distinctions and limits

- 931 primary +31 numeric supplement +4 separately labelled valid controls: 966 effective fixtures,591 accepted/375 rejected;577 newly executed and14 validation-only in those manifests. Original two expectation errors and first results remain preserved.
- Historical576 and retained120 pass;24 retained M11 execution targets carry accepted exact-target proof.
- Exact98:67 validated,31 rejected,48 execution-qualified,19 validation-only. Candidate1 newly executed the five full sources/40 dispatches; candidate2 carried all48 source proofs after exact target identity, rather than claiming new dispatches. This is +15 validated sources over M11, not +17.
- Fresh native retained74, numeric200 and ASan/UBSan128 commands pass. All448 native and224 sanitizer targets independently validate/execute.
- Complete strict-PIC stack graph: semantic SCC1488, other-frame sum16264. Complete unchanged-Makefile graph:1520 and16368. Limits remain1536 and16384; normal-profile margins are only16 bytes. All other ceilings/layout/ABI/pools remain unchanged. Requalify other compilers/flags instead of extrapolating.
- No physical GPU/backend numeric, TSan, performance, whole-language/full migration or production/default Zig switch claim. N1 exact-final AbstractInt dot remains the selected interpretation with disclosed Tint disagreement/CTS gap, not settled upstream consensus.
