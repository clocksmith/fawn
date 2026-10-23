# Portable sealed M10 native replay

Import `replay_native_m10.py`, plus the original sealed native directory's
`SOURCE_SEAL.sha256` and all15 entries named by it. The wrapper verifies that
manifest's preinspection hash `de784f1927dd9abde7272d60c3e3983fd0b65f6f8738c78f1ddc3923acfcd7dc`
and every original file before loading the original source-only runner.
No frozen test, fixture or expectation is changed. This wrapper is supplemental
post-inspection orchestration, not a new preinspection seal.

```sh
python3 replay_native_m10.py \
  --tests-root /path/to/sealed-native \
  --source-root /path/to/verified-v6/runtime/c/compiler \
  --candidate-manifest /path/to/candidate-v6-final-source.sha256 \
  --evaluate-candidate \
  --out /path/to/fresh-native-receipts \
  --build-root /tmp/fresh-native-build
```

The final v6 manifest is fixed to
`e818441e6c93feb04a7722fc1602f255cfd2e596d806604821aa7bbd178e1e11`.
All30 source hashes and frozen public headers are checked; unmanifested build
inputs are rejected. The original runner checks hashes again after compilation.
Archive/shared/C11/C++17/fenv/errno/concurrency/allocator/fault/ASan+UBSan routes
run from source, without any prebuilt library. Sanitizers are on by default;
`--skip-sanitizer` explicitly omits them. Existing output/build directories are
not overwritten. Python3.10+, GCC/G++, ar, nm and readelf must already be installed.

Only C harness builds containing `allocator_m10.c` receive `-fno-builtin`, the
same documented allocator positive-control correction used in the actual final
59-command v6 run. Candidate flags/source, syntax commands and C++ flags stay
unchanged. No install/network/physical-GPU operation is involved.

Without `--evaluate-candidate`, the wrapper only verifies a command plan and
input hashes. With `--syntax-only --out <fresh-path>`, it syntax-checks the
independent sealed test sources against their frozen headers, without compiling
or executing candidate code. The portable wrapper was plan-checked and syntax-
checked; its underlying final v6 native test run already passed59commands and
15consumer suites. The wrapper itself was not redundantly fully rebuilt.
`REPORT.json` is the unchanged runner's report; `PORTABLE_REPORT.json` separately
labels its candidate identity as a manifest rather than a Git commit.
