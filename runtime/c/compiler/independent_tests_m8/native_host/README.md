# Independent M8 native and host environment evaluator

Source-only fixtures are frozen in PRE_CANDIDATE_FREEZE.json and CLI_PRE_CANDIDATE_FREEZE.json before the first candidate inspection. Historical M7 helpers are imported/referenced unchanged, with accepted-commit hashes checked before execution. There is no embedded candidate binary or copied implementation tree.

Run from a checkout retaining the accepted independent_tests_m7 sources with installed GCC/G++14.2 and an existing spirv-val:

```sh
python3 run_native_m8.py \
  --source-root "$REPO/runtime/c/compiler" \
  --retained-root "$REPO" \
  --prebuilt "$REPO/runtime/c/compiler/build/libdoe_compiler.so" \
  --out "$EVIDENCE/native-fresh"

PYTHONPATH="$REPO/runtime/c/compiler/independent_tests_m7" \
python3 run_cli_numeric_m8.py \
  --source-root "$REPO/runtime/c/compiler" \
  --retained-root "$REPO" \
  --validator "$INSTALLED_SPIRV_VAL" \
  --out "$EVIDENCE/cli-fresh"
```

Use fresh output directories, preserving first failure receipts. Native orchestration independently compiles every library C source into an archive and a shared object, records source and generated-artifact hashes, and labels optional supplied-prebuilt runs separately. CLI fault binaries are independently source-built with the unchanged historical ownership wrapper and additional narrow environmental-failure hooks. The numeric source is the exact source in fixtures_m8.h/numeric_fixture_m8.wgsl; the expected output bits are frozen mathematical values. Native constant presence and cross-environment identity checks complement, but do not replace, the parent's independent emitted-instruction execution.

ASan/UBSan runs use the same candidate source; LeakSanitizer is explicitly disabled. The supported host proof is Linux x86-64/GCC14.2 with the frozen IEEE representation checks, not a claim of another host, GPU execution, performance or complete migration. Explicit --skip-sanitizer records reduced coverage and must not be described as full sanitizer acceptance.

Candidate1: native-report.json records first-run PASS, complete source-built/archive/shared/prebuilt distinction, zero allocator calls/imports, unchanged public headers/exports,2368 host-environment calls per exercised build,60 applicable native fault cases and retained native guard/mutation coverage. Numeric CLI results.json records174 passing ordinary/sanitized checks. SOURCE_AUDIT.md records private layouts, actual libc/libm dependencies, dependency traversal and conservative recursion sums. These candidate-specific results are separate from frozen source expectations.
