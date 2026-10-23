# Independent Instance 1 acceptance package

See FINAL_REPORT.md first: PASS only for the pinned experimental Linux x86-64
ReleaseFast CPU/host boundary. Debug, GPU/device/presentation/imported textures,
package/browser adoption and complete migration remain unqualified. Native
sanitizers instrument seven C units, not retained Zig/stubs/system libraries;
LeakSanitizer is disabled. The original FCI timeout has unknown child lifecycle,
and the successful isolated retry is not claimed proven uncontended.

This package contains exact independent fixture/driver source, original sealed
cases, historical fixture versions, immutable source manifests, and compact
execution/failure receipts. It contains no candidate implementation snapshot,
binary, cache or toolchain. Detailed original receipts and all source snapshots
remain in the separately retained evaluation evidence. Receipt compaction hashes
the original full file and retains actual commands/results; long outputs and
sandbox-parent process descriptions are explicitly summarized, not silently
presented as complete logs. The compact receipt index names every source receipt.

## Explicit source identity and portable entrypoints

Use GCC/G++ 14.2, Python/Make, Zig 0.15.2 and an existing Vulkan loader/link prefix.
No installation or physical GPU is part of this recipe. Use fresh private output
folders and serialize native compilers. The source root must match the selected
manifest before run_native_serial.py copies its inputs.

Set absolute shell variables REPO, OUT, RECEIPTS, ZIG, LOADER_PREFIX, and MANIFEST.
The original manifest is NATIVE_COMPLETE_INPUTS.sha256 (1,693 +26 entries).
The native driver REQUIRES an explicit --source-manifest; for later acceptance
navigation-only changes, pass a separately reviewed/bound manifest override.
Do not rewrite the original manifest to current HEAD or silently incorporate
compiler/Makefile changes. Compiler M10/M11 is outside this dependency set.

Build each C archive with GCC only:

    sh "$REPO/runtime/c/instance/integration/build_archive.sh" "$OUT/instance"
    sh "$REPO/runtime/c/callbacks/integration/build_archive.sh" "$OUT/callbacks"
    sh "$REPO/runtime/c/futures/integration/build_archive.sh" "$OUT/futures"

Reproduce public independent C/C++ tests with fresh output/receipt directories:

    python3 tests/run_public.py --repo "$REPO" --out "$OUT/public-normal" --receipts "$RECEIPTS/public-normal" --mode normal

Repeat with distinct folders and --mode asan or --mode tsan for the separately
instrumented standalone C owners. ASan uses detect_leaks=0, explicitly not LSan.
Do not supply prebuilt archives when claiming an independent source build.

Reproduce the sealed 12-test native fixture and four canonical normal controls:

    python3 tests/run_native_serial.py --repo "$REPO" --out "$OUT/native" --receipts "$RECEIPTS/native" --source-manifest "$MANIFEST" --instance-archive "$OUT/instance/libdoe_instance.a" --callback-archive "$OUT/callbacks/libdoe_callbacks.a" --futures-archive "$OUT/futures/libdoe_futures_zig.a" --zig "$ZIG" --loader-prefix "$LOADER_PREFIX"

--skip-controls and --only-controls/--lanes select explicit subsets, never infer
unrun coverage. The driver uses -j1 and a 900-second bound. For supervised retries,
monitored_build_v2.py supplies a new process group, same-wrapper process/resource
sampling and terminal group checks. A missing session is not quiescence evidence.
Never overlap an uncertain old compiler silently or replace source acceptance
with author-prebuilt consumption.

## Additional exact obligations

Execution-specific continuation drivers are retained byte-for-byte in tests/;
their original absolute paths are evidence, not portable defaults. Their compact
receipts record argv/cwd/environment; substitute explicit source/archive/tool/
output roots when reproducing, preserving all flags, tests and expectations.
In particular:

- run_native_regressions_monitored_v2.py + resume_native_regressions.py preserve
  F11, FC11+5, FCI23 and default24/ABI1. The final default root is
  default_regressions_v3.zig with default_regression_filters_v2.json, all C flags
  false. Earlier roots are retained only as failed/omitted-coverage history.
- native_labels.c covers 64 lifetimes; native_wgsl_control_v2.c is the corrected
  default-matched WGSL control. Earlier WGSL fixture documents its baseline proc
  assumption failure. Missing WGSL proc routes do not count as executed.
- run_build_boundaries_monitored_v2.py contains 13 exact explicit rejection cases.
- run_named_roots_v2.py builds dropin-compute and dropin-full with all three
  explicit archives; those artifacts were NOT executed.
- Native sanitizer archives contain exactly the seven named C units. The final
  ASan binary requires the GCC ASan runtime first (LD_PRELOAD only on the test
  executable, not the compiler); resume_native_sanitizers.py records the separate
  source build/run and TSan lane. NATIVE_SANITIZER_ENVIRONMENT.json fixes options.
- Private count/status fixture commands and fatal expectations are in compact
  private-boundaries/status-boundaries receipts; they do not add a public API.

Original FIXTURES_SEAL*.sha256 files are historical statements. Verify each old
version in its matching pre-* directory, not against the newer active tests.
CASES_SEAL.sha256 still verifies the original concrete inventory and baseline
receipt. IMPORT.sha256 verifies this whole compact package. The original source
manifest is repository-root relative; it is not a package file-content manifest.

No new native run is claimed merely because this package was constructed.
