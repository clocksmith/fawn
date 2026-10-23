# Portable M11 native replay preparation

This is a separate replay wrapper, not a new acceptance receipt. All original219 sealed native files, the first113-command failure receipt, original supplement and exact corrected `boundary_m11_revision1.c` remain unchanged. No historical corpus is copied.

## Inputs and host

Use Python3 with GCC/G++14, binutils and the native Linux x86-64 environment qualified by the original receipt. Keep the original native evidence directory, the17 frozen work cases, an immutable candidate owner tree and its original `SOURCE.sha256`. Supply a Doe checkout containing the161 repo-relative historical inputs pinned in `PORTABLE_NATIVE_INPUTS.json`. The wrapper verifies those hashes, the original219-file native seal, the candidate manifest/source and the four headers before preparing commands. New hosts/toolchains require their own qualification.

All five paths are explicit. Run from any working directory; choose NEW, separate build and output directories outside the immutable input trees:

```sh
python3 /path/to/native/portable_native_replay.py \
  --repo /path/to/doe \
  --owner /path/to/candidate/runtime/c/compiler \
  --manifest /path/to/candidate/SOURCE.sha256 \
  --build-root /tmp/m11-replay-build \
  --out /tmp/m11-replay-receipts
```

By default this only verifies identities and writes PLAN.json, IDENTITIES.json and RESULT.json (`PREPARED_NOT_EXECUTED`). It does not run a compiler, binary or test, and does not create the build directory. `--evidence /path/to/native` and `--cases /path/to/frozen` may override their script-relative defaults. Paths containing quotes, backslashes or newlines are rejected because preserved Python command recipes embed path literals.

For a subsequent explicitly intended replay, use fresh destinations and add `--execute`. This invokes the unchanged strict `build_m11.py` with every path/manifest hash explicit, then uses that single fresh archive/shared/CLI build for:

- Exact corrected boundary harness, archive/shared routes.
- Public default work cap and no-barrier twin, archive/shared routes.
- Private link-time N/N-1 controls for17 frozen witnesses/probes and all four adapters, including scratch poisoning.
- Actual layout measurement, unchanged retained stack parser, original corrected full-ledger/call-graph audit and retained target-ID format boundary.

Add `--sanitize` to include original ASan/UBSan source-build recipes and affected controls. Add `--retained` to replay the preserved M10/older native C/C++/query/host commands against their existing hashed source paths; it never copies the historical corpus. Add both `--prebuilt /path/to/lib.so --prebuilt-sha256 EXPECTED_SHA256` for a separately attributed prebuilt route. No prebuilt is assumed or silently substituted when omitted. Shared-library runtime paths follow the supplied artifact path; a hashed prebuilt must remain ABI-compatible.

The old known-bad boundary commands are explicitly not scheduled; their original records remain intact. Corrected boundary commands come from `candidate2-boundary-revision1/commands.json`, using byte-identical corrected source. Recorded commands change only original absolute repo/source/build/evidence/prebuilt paths and the Python interpreter path. The ledger script is copied only into the NEW receipt directory; its two original absolute owner/build literals are relocated, and original/relocated hashes are recorded. No test expectation, assertion, fixture or candidate source is rewritten. The new orchestration adds the already-used private work/layout/ID commands without modifying those harnesses.

Execution is fail-fast, preserving COMMANDS.json, each log and FIRST_FAILURE.json. A result is scoped only to selected controls. The wrapper does not run target interpretation, the same98 corpus, GPU/runtime checks, target opcode counting against the parent's emitted binaries, or finalize/overwrite any original acceptance report. Original overlap-precondition and external-stack exclusions still apply. Preparation success is not replay success.

## Preparation performed here

Only Python syntax and identity/plan preparation were checked. The full-option plan contains134 commands, routes all four boundary builds to the exact corrected source, and creates no compiler build directory. No compilation or gate rerun occurred. `PORTABLE_NATIVE_PREPARATION.json` records this limited result. The new portable files have their own `PORTABLE_NATIVE_FILES.sha256`; the original `NATIVE_ARTIFACTS.sha256` is unchanged.
