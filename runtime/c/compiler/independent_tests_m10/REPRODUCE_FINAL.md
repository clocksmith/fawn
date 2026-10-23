# Portable source-only M10 evaluator

Requires existing Python3, GCC/G++14, binutils, pthread/libm and approved spirv-val. No install/download, Zig runtime build, physical GPU or private trace is required. All compilation outputs and caches should use a fresh task-private /tmp directory. Keep original source and receipts intact.

1. Verify `CONCRETE_SOURCE.sha256` and native/SOURCE_SEAL.sha256. The original files must not be edited. Read POSTDISCLOSURE_ACCOUNTING.md for the exact later adapters.
2. Materialize accepted historical inputs from a repository containing commit dee5f42acbb50e05d5690f1d2d13777c5c26d51d:

    python postdisclosure/prepare_retained.py --repo REPOSITORY --out /tmp/m10-retained-fresh

3. Extract/verify the candidate archive against its complete source manifest. Final accepted source manifest is author/candidate-v6-final-source.sha256, SHA256 e818441e6c93feb04a7722fc1602f255cfd2e596d806604821aa7bbd178e1e11. No moving whole-checkout identity is assumed.
4. Run the full main source-only route. OWNER is the extracted runtime/c/compiler directory and MANIFEST the exact30-entry source manifest. The runner verifies every bound file before/after, builds a new library/archive/CLI from source, executes main/native-CLI, historical, same98, mutant, capacity, ID and frame/record gates, preserving all commands and outputs:

    python postdisclosure/run_portable.py --owner OWNER --source-manifest MANIFEST --retained-root /tmp/m10-retained-fresh --validator SPIRV_VAL --out /tmp/m10-replay-fresh

This uses the deliberately enumerated adapters, not a claim that unchanged old oracles understood the new target representation. The retained same98 consumer is unchanged. Every normal target validation explicitly uses --target-env vulkan1.1. The large width-only probe separately records its raised validator resource limit.

5. Run the sealed native suite and enumerated retained-native runner documented under candidate-v6/native and candidate-v6/native-retained. Their original failures/provenance stay separate from corrected replay. STACK_AND_RECORD_AUDIT.json replaces obsolete private M9 record measurements, not public ABI expectations.

The primary independently built final artifacts were /tmp/doe-m10-evaluator-v6-stack; hashes are in candidate-v6/FRESH_BUILD.json. Main and historical source-only executable inputs live under source/, postdisclosure/, candidate-v3/postdisclosure_semantic_adapter.py, and the fixed accepted evaluator imports. No evaluator-private production source or prebuilt compiler is required for reproduction.
