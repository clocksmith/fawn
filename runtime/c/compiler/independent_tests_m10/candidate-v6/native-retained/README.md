# Retained M9 native tests on final M10 v6

This is **post-inspection orchestration**, not a replacement preinspection seal.
The original M9 tests at fixed commit `dee5f42acbb50e05d5690f1d2d13777c5c26d51d`
and sealed M10 native tests are never edited. `RETAINED_INPUTS.json` binds all
142 retained source/import files used here. The final v6 source manifest is
fixed to SHA-256 `e818441e6c93feb04a7722fc1602f255cfd2e596d806604821aa7bbd178e1e11`.

## One source-only command

With Python 3.10+, GCC/G++, ar, nm and readelf already installed on Linux:

```sh
python3 replay_retained_native.py \
  --source-root /path/to/verified-v6/runtime/c/compiler \
  --retained-root /path/to/verified-dee5 \
  --candidate-manifest /path/to/candidate-v6-final-source.sha256 \
  --record-audit /path/to/STACK_AND_RECORD_AUDIT.json \
  --out /path/to/fresh-receipts \
  --build-root /tmp/fresh-retained-build
```

The optional `--record-audit` links the separately source-bound M10 private
record/stack proof. Its absence does not claim those private checks passed.
Sanitizers run by default. `--skip-sanitizer` explicitly records the omission.
No prebuilt candidate library, package download, network, or GPU is used.
`--out` and `--build-root` must not exist. No previous output is removed.

The runner verifies all142 historical inputs and all30 candidate entries before
and after. It first runs the unchanged old runner and retains its original
failure logs, then generates isolated overlay sources and runs all native
archive/shared/C/C++/fenv/errno/allocator/concurrency/ASan+UBSan checks.
The generated runner, source copies, every command and exact adapters are kept.
Candidate compilation flags remain the pinned runner's strict C11/O2 flags;
only harness compilations add `-fno-builtin`, correcting the documented GCC
positive-control allocator-store optimization. Sanitizers add
`-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined`; harness executables
also use the retained `-fno-pie -no-pie` flags.

## Enumerated reconciliation, no broad status relaxation

1. Exactly three old fixed-workspace upper-bound assertions change122880 to
   frozen147456 bytes (M10 section6/storage ledger).
2. The wrapper frame bound changes123136 to frozen147968; all other8192-byte
   frame bounds stay unchanged. Exactly7 exports replace exactly5, requiring
   all five old names plus both additive ABI4 names.
3. One exact M6 seed, row9, now permits a whole-vector local snapshot (M10
   sections2/8). SHA-256:
   `ba88f71337626997c0a81f48f7e9eb540ca36b04456195cc0594defeb078f26c`.
   All other negatives and all5120 mutations remain checked.
4. Two distinct exact uniform-array fixtures remain invalid, but status3 becomes4:
   M10 section4 admits the type while rejecting conservative-uniform stride4.
   M5 fixture SHA `8cf616ceed349c909209b37a777a8b66bd58c8117e260958b1dddba541d678b6`;
   M9 public-exit kind8 SHA `890fcacb0d44a98b7e845ca7627d164cb0c89162a2c7d7da4493e6bb5433369e`.
   Every other source/status and all atomicity/errno/full-FP assertions remain.
5. The obsolete private-layout probe actually fails compilation against changed
   private structures. That original failure is retained and **not counted as a
   pass**. The separately measured frozen M10 record/combined-stack audit is the
   replacement, not an altered old layout expectation.

`original_run` preserves the first untouched runner attempt. `adapted_run`
preserves the initial overlay's distinct public-exit first failure. Its final
exact correction is proved for archive/shared/sanitized in `final_public_exits`.
`portable_run_1` is the complete final one-command reproduction, including a
fresh untouched-original replay. None of these earlier attempts is overwritten.
The result is a native-host lane result, not semantic/corpus/GPU acceptance.
