# Independent milestone 5 checks

This source-only evaluator freezes contract-driven checks for flat scalar uniform resources and caller-owned compiler workspaces. It is independent of the implementation. CPU SPIR-V instruction execution and target validation do not qualify physical GPUs, runtime adoption, performance or full migration.

The inventory was frozen before candidate implementation inspection against contract commit d618a225ef6bb4f8eb476dc33e05038ecaf2a3ac. FROZEN_CASES.md SHA-256 is 379837150d1569a0b21eb3cce191d5c8d1be14c08b8345c218b9959470186cac. FREEZE_ERRATA.md preserves narrowly corrected evaluator errors; no malformed/type-invalid expectation was relaxed. Historical selected M4 source files remain byte-identical and are attributed in IMPORTED_SOURCE.json. LICENSE preserves the repository's MIT license.

## Run with existing tools

From this directory, substituting real paths:

    python3 run_acceptance_m5.py --repo /path/to/doe --validator /path/to/installed/spirv-val --evidence /path/to/fresh/evidence

Optional --source-root selects an immutable compiler-owner snapshot. The default is the current checkout's runtime/c/compiler. --cc selects an existing GCC executable. GCC, G++, Python and ordinary Linux inspection tools must already be installed; nothing is downloaded. The validator is always invoked with --target-env vulkan1.1.

Optional --baseline-library and --baseline-cli add direct comparisons with preserved M4 artifacts. They are not required: source-hash-bound M4 storage SPIR-V and legacy JSON goldens are included. --plan resolves inputs and prints commands without executing them. The runner preserves stage logs and first failures and stops at a failing stage. Output directories must be new.

All imports are ordinary local imports, without sys.path edits. SOURCE_FILES.json is the complete portable file list; SOURCE.sha256 seals it. No compiler binary, machine-specific artifact directory, historical nested suite tree, or network service is required.

## Stages and limits

- semantic_m5.py: native API2, exact reflection including zeroed unused bytes,32 positive/51 negative cases,735 host-packed-byte instruction trials, short-uniform pre-admission and9 defective-module controls. Uniform values come from independent host bytes; loads use emitted resource identity, member index and Offset decorations, not compiler reflection as an execution oracle.
- blackbox_m4.py: unchanged selected M4 suite,1891 checks and17625 instruction trials; API1 semantics/boundaries and16-thread calls. test_oracle_m4.py retains12 mutation controls and40000 finite arithmetic oracle cross-checks.
- retained_equivalence_m5.py:270 retained positive source-bound SPIR-V outputs remain identical from old ABI1 to current ABI2.
- cli_m5.py:599 ordinary/sanitized checks, exact v2 JSON, legacy bytes, routing, query/allocation/I/O faults, no-clobber and owned rollback. No baseline binary is needed.
- corpus_m5.py:98 original paths/91 unique contents through the unchanged actual emitter function with unique outputs; no full gate-main/EMIT-SKIP inference. corpus_semantics_m5.py executes the actual five emitted modules against frozen source-matched expectations. corpus_phases_m5.py separately records lexing, parsing, semantic validation, emission, target validation, instruction execution and absent physical qualification. Static missing-feature inspection is not the observed compiler failure stage.
- native_stage_m5.py: strict source/prebuilt C/C++ consumers, layout/guard/reuse/thread/no-allocation/ASan/UBSan/fuzz, exports and source-bound stack/callee receipts. Native corpus comes from the semantic stage. LeakSanitizer is disabled in the exercised ptrace environment; explicit allocation/free instrumentation remains on.

The8192-byte native-core frame budget applies to the v2 library entrypoint and library callees; ABI1 wrapper has its separately frozen105328 budget. CLI main retains its separately reported107728-byte caller-side source/word/binary/path frame, not a hidden compiler workspace. These are per-frame measurements, not an observed combined stack high-water mark.

Logical empty storage vectors test robust-load-zero/store-discard semantics; they are not claims of physically legal zero-byte bindings. Uniform buffers must be admitted at their full independently calculated minimum extent; three scalar fields require12 bytes, while the actual four-field package witness requires16. Natural struct alignment4 and conservative uniform aggregate alignment16 are distinct.
