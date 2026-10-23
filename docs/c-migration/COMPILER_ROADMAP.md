# Compiler ownership remaining after the scalar seed

This is the dependency order for the authorized full C migration, not a new
support claim or an immutable list of shader-specific milestones. The existing
[migration gates](MIGRATION.md) remain blocking. Keep one typed pipeline and
extend reusable owners; do not recognize filenames or copy source fragments into
specialized emitters. All production compiler owners remain retained Zig today.

## Accepted function-state prerequisite

The [accepted M7 increment](../../runtime/c/compiler/MILESTONE_7_RESULTS.md)
owns bounded function-local mutable state and structured iteration. The unchanged `moderate_matmul_naive.wgsl`,
`dot_product_forward_f32.wgsl` and `dot_product_reverse_f32.wgsl` exercise generic
initialization, assignment, repeated loads, accumulation, for/loop termination,
break and post-loop use. They are witnesses, not the admitted grammar. The
accepted statement/control family includes lexical scoping, typed values,
evaluation order, robust accesses and independent instruction execution.
Loops remain loops, not source-specific host computation or
compiler unrolling to an assumed uniform value. Execution-step limits belong to
test harnesses, not silently changed shader semantics.

Scalable job storage now has an explicit independently tested owner. The current
source/token/node/statement limits and fixed public reflection arrays protect
the seed API; they are not permanent language requirements. M7 uses explicit
per-job capacities with checked layout arithmetic, failure and reuse.
Preserve ABI1/v2 layouts and applicable storage/reflection behavior
through shared-pipeline compatibility adapters, not a second compiler or hidden
large stack workspace. Any allocating convenience path needs a distinct,
documented OOM result and fault tests. Avoid increasing private arrays indefinitely
or treating historical unsupported cases as permanent grammar promises. General
numeric/type/module ownership is the next dependency to scope from the remaining
81 source failures; first lexical failure alone does not describe all missing
semantics. Physical backend qualification remains independent.

## Remaining dependency map

| Coherent owner | Retained source seam | Completion evidence still required |
| --- | --- | --- |
| Source/token/numeric admission | `wgsl/frontend/lexer.zig`, `token.zig`, parser numeric owners | All declared operators/literals, original spans, contextual identifiers, malformed/overflow cases and explicit Unicode policy; lexical admission alone is not semantic support. |
| Module, type and scope model | `frontend/ast.zig`, `parser_decl.zig`, `sema*.zig` | General declarations/order, named types, signed/bool/abstract values, conversions, constants/overrides, selected entries, function signatures/calls, recursion rejection, namespace/shadowing rules and typed diagnostics. |
| Function control and values | `parser_stmt.zig`, `parser_expr.zig`, `ir/ir_builder*.zig` | Mutable locals, all selected structured branches/loops and abrupt exits, live/dead validation, dominance, evaluation multiplicity, reusable typed IR and checked storage. |
| Aggregate and address-space layout | `ir/layout_utils.zig`, resource/type validation and target layout owners | Vectors, matrices, nested structs, fixed/runtime arrays, pointers, access permissions, storage/uniform/workgroup distinctions, explicit padding/stride and per-access robustness. |
| Builtins and collective semantics | semantic/IR builtin owners and target emitters | Typed math, conversions/packing, textures, atomics, barriers/subgroups, uniformity and extension admission; no invocation-order inference from a sequential oracle. |
| Analysis and transformation | `pipeline/analysis.zig`, `ir_validate.zig`, robustness/proof/rewrite owners | Owned module and diagnostic lifetime, allocator failures, override/robustness policy, arithmetic/reduction identity, cache/config identity and target-independent invariants. |
| Target artifacts | `emit/spirv`, `emit/msl`, `emit/hlsl`, `emit/dxil`, `emit/csl`, browser rewrite/WASM surfaces | Each declared target's actual consumer, format validation, supported behavior and failure boundary; installed tool prerequisites and physical execution remain separately qualified. |
| Runtime translation metadata | `wgsl/runtime/runtime_translation_info.zig`, `wgsl/pipeline/binding_reflection.zig` | Per-entry resources, minimum sizes, `needs_sizes_buf`, buffer/texture dispatch preconditions, clone/free/partial-failure and cache publication. Standalone reflection is not this owner. |
| TSIR/CSL planning | `compiler/tsir`, `compiler/targets`, `docs/tsir-contract.md` | Semantic versus realization identity, planning, reference parity, target descriptors and real-kernel acceptance; simulator/bootstrap evidence remains distinct. |
| Compiler callers/build ownership | public `@import("doe")` exports, native shader/pipeline requests, CLI/package/browser/build roots | Every current caller has an exercised C binding or explicit migrated replacement, no Zig implementation dependency in the intended C product, and retained diagnostics/artifact/config semantics. |

Paths in the table are relative to `runtime/zig/src/compiler` unless otherwise
named. The inventory is the exact baseline source list; this map groups behavior
rather than requiring one C file per Zig file. Before selecting each owner, trace
its actual callers and focused retained tests, then freeze the newly admitted
behavior and failure boundary. Later changes may refine dependency order without
marking an untested owner complete.

## Coverage and promotion

Keep the same source-bound corpus ledger through lexical admission, parsing,
semantic validation, emission, independent target validation, actual instruction
execution and physical backend qualification. Add wider retained compiler suites
as their owners move; 98 example sources are not the whole compiler corpus.
Record intentional old rejection lifts individually. Malformed/type-invalid
cases remain failures, not obstacles to a pass percentage.

A larger C coverage count does not retire the corresponding Zig source. Complete
compiler ownership requires all rows plus the migration manifest's consumer and
target gates. Runtime/GPU qualification is independent, and no missing physical
device is replaced by CPU interpretation or software validation. Production
selection and public publication remain outside this isolated implementation.
