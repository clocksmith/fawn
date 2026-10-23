# Current independent M9 acceptance gate

The one supported source-only front door is `run_acceptance_m9_current.py`.
Preserve this `source` directory and the sibling `native_capacity/source`
directory, or explicitly supply `--native-source`. Retained evaluator imports
come from the given Doe checkout, not an evidence archive.

    python3 /path/to/source/run_acceptance_m9_current.py \
      --repo /path/to/doe \
      --source-root /path/to/doe/runtime/c/compiler \
      --native-source /path/to/native_capacity/source \
      --validator /installed/path/to/spirv-val \
      --evidence /fresh/output

Dependencies: Python3.12 standard library, GCC/G++14.2, binutils, libc/libm,
and the already-installed SPIR-V validator used by this campaign. No Zig,
download, private binary archive, GPU, new package, or developer test is needed.
Use `--plan` to print every command without execution. `--prebuilt /file.so`
adds explicitly separate supplied-library consumer evidence; the source-built
library/archive are still required. `--stages` is a focused run, never a claim
that the complete gate passed. Output must be a fresh directory. `--native-compact` selects the guarded low-disk native orchestration with unchanged checks.

This current gate runs independent interval and instruction controls, four
unfinished-adapter controls, all380 new semantic cases,12 valid new mutants,
839 retained historical cases,997 retained M8 cases,17 retained valid mutants,
31 retained capacity and8 postdisclosure cases, the unchanged full98 consumer,
and native/capacity/CLI checks. It builds from source with strict FP/warning
flags and binds all production inputs before/after the complete run.

## Preserved narrow evaluator corrections

Original seals, tests and first-failure receipts remain intact. The current
runner selects separately named adapters, not modifications of frozen files.

- `retained_m9_corrected.py` restores the historical `unfinished=True` handling
  accidentally omitted by the M9 vector adapter. Only already-tagged unfinished
  cases use the unchanged M8 vector routine with the M9 StepLimit identity.
  Four independent controls require finite success, finite StepLimit failure,
  unfinished StepLimit observation, and unexpected-termination failure. All
  finite vectors still use the exact presealed M9 routine.
- Native fixed-memory assertions use the explicitly reviewed122880-byte M9
  ceiling, rather than superseded M6/M7 private-workspace ceilings. No public
  capacity, source, expected status, redzone, sanitizer or semantic check is
  relaxed. The native POSTINSPECTION_CORRECTIONS.md records exact derived files and repairs.
- The validator's configured large-function resource allowance is not a WGSL
  language limit. Large-formal positives use explicit Vulkan1.1 plus
  `--max-function-args 65532`. Default-resource observations stay separately
  labeled and never replace the required configured validation result.

The original README and seals document frozen provenance, numeric proofs,
intentional named rejection lifts and the zero-tie policy. The current runner
is a postinspection orchestration addition, not a replacement preinspection seal.

## Storage-only local orchestration

The campaign additionally used `compact_semantic_m9.py` around selected fresh
retained/corpus runs. After a synchronous validator returns, it losslessly gzip
compresses only a newly generated raw module in its own fresh output root,
verifies persisted roundtrip SHA256, and records original path/size/hash,
validator command/result and compressed bytes in `compression_manifest.json`.
Modules needed by pending M8 mutations are explicitly kept raw. After those consumers finish, completed M8/historical module files can also be consolidated into verified lossless tar archives to reclaim filesystem block overhead; their exact per-path manifests and restoration instructions remain beside all original JSON receipts. An original
module can be recreated with `gzip -dc module.spv.gz > module.spv`; verify its
SHA256 against the manifest. Old evidence, source, seals, final DSO/archive and
prebuilt observations are not removed. The compact wrapper changes storage,
not fixture sources, oracle values, validator flags or stage expectations.

Fresh emitted modules, prebuilt observations and65 consumer rejections are
separate counts. CPU instruction execution is not physical GPU execution.
No backend qualification, adoption, performance parity or full-migration claim
follows from this compiler gate.

## Provenance seal versus portable manifest

PRE_CANDIDATE_SEAL.json is the unchanged original full-evidence provenance seal.
It references frozen inventories and control receipts in the original evidence
root; those generated files are not all shipped in the portable source package.
It is not the portable package completeness manifest. The complete current
source/ payload is bound by source/CURRENT_SOURCE_MANIFEST.json; the sibling
native package has its own current and original fixture manifests. The current
runner verifies its source manifest and139 unchanged historical imports, then
regenerates exact fixtures and independent expectations from the sealed sources.
No original evidence directory is needed to reproduce the acceptance checks.
