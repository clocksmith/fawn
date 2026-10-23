# C compiler milestone 1 contract

Frozen 2026-10-23 before implementing the first migration expansion. Source baseline 665475907494d2095a760596218b4c2d363cb436. This replaces neither production Zig nor its compiler interface.

## Real source and seed

Compile unchanged `bench/inference-pipeline/kernels/residual.wgsl`, byte-identical to `bench/kernels/compilation-corpus/simple_residual_add.wgsl`: three f32 storage buffers, invocation-x local alias, and `out[idx] = a[idx] + b[idx]`. Preserve the prior independently tested u32 increment and optional named-u32-constant subset from the dot language experiment. Import only copies; preserve original evidence and record hashes/licensing.

## Admitted first expansion

- One optional `const NAME: u32 = Ku;` followed by 1..8 module storage declarations, each `@group(G) @binding(B) var<storage, read|read_write> NAME: array<u32|f32>;`. G0..3, B0..31; binding pairs unique. Attribute order may vary. Storage names/module entry/constant obey WGSL scope and cannot shadow referenced types.
- One compute entry, one global_invocation_id parameter of vec3<u32> or vec3u, local size1..256. Optional one `let IDX = PARAM.x;` local index alias. Exact same invocation-x index is required for accesses, written either PARAM.x or IDX when declared.
- Exactly one storage assignment. RHS is one addition of two operands: a storage element load, a decimal suffixed u32 literal, or the declared u32 constant. Both operands and destination must have the same scalar type. At least one operand must be a storage load. Named parameter/alias shadowing never accidentally resolves a module constant. Target storage must be read_write.
- F32 buffers support F32 load+load addition, not new floating literal syntax. U32 addition wraps. No extra operators, general expressions, casts, statements, structs, functions, stages, arrays or fallback enter silently. Existing lexical whitespace, nested comments, ASCII identifier/reserved-word rules and original positions remain.
- Limits:65536 source bytes,2048 source tokens,63-byte names,8 buffers,4096 output words. Increased limits are explicit ABI capability constants; all oversize cases reject.

## Behavioral and API contract

A standalone C11 library and CLI, built with installed GCC without Zig anywhere in generation/compilation/link/execution. New API `include/doe_compiler.h` identifies this evolving compiler, declares ABI version1, caller-owned output/result and bounded per-binding reflection. The call checks explicit ABI version and result byte-size before writing any result; mismatch returns ABI_MISMATCH with both result and words untouched. NULL result returns INPUT_ERROR. Calls must be independently safe in parallel. Reflection includes all declared bindings, access, scalar/stride/runtime-array, entry and local size. An input of size0 rejects; non-NUL-terminated input is valid; NULL+positive length rejects. No library heap allocation, global mutable state or retained pointers. Errors carry category, original byte offset, line, column and useful bounded message; failure zeroes reflection/count and leaves words unchanged.

Emit SPIR-V1.3 valid for Vulkan1.1, with f32 or u32 typed arithmetic. Each out-of-range load resolves to typed zero; each out-of-range store is discarded. Check each resource's own length, including unequal/empty buffers. Do not issue speculative out-of-range accesses before a guard. Distinct declared resources remain distinct descriptors, even if names/order change. This defined robustness policy is not inherited physical-driver evidence.

CLI: `--shader-path INPUT --out OUTPUT --reflection JSON`. Preserve existing artifacts on every failure; refusing existing destinations is allowed. Publish complete reflection from IR. Source and output identities are recorded by external test receipts; no substitute source is compiled.

## Completion gate for this increment

1. Both unchanged real residual copies and all retained seed-positive cases compile; exact reflected bindings/types/access/local size match.
2. Independent CPU interpretation of emitted instructions checks float32 addition, finite normal/subnormal inputs, signed zero, infinities/NaNs by classification rather than payload, u32 wrap, empty/unequal lengths, untouched tails and large invocation indices. No GPU claim.
3. Independent spirv-val Vulkan1.1 accepts every admitted generated module; invalid source, type/access/scope/binding collisions, counts, capacity, diagnostics, output atomicity and CLI failures are tested.
4. ASan/UBSan compile and malformed-input/boundary harnesses pass; LeakSanitizer unavailable is disclosed. Ordinary GCC C consumer source-build/load/call passes with no Zig dependency.
5. Architecture review: small separately owned lexer/parser/sema/IR/emitter/API/CLI files, no macro framework/template recognition. Manifest marks this as a compiler increment only, not a migrated production owner.

Only existing approved tools are used. No device/runtime/backends are added in this increment. Full migration continues through separately declared compiler coverage and backend/lifetime/consumer gates, not by widening this pass claim.
