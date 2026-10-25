/* SPDX-License-Identifier: MIT */
#include "private.h"
#include "../../src/builtin.h"
void metal_subgroup(MetalEmitter *e, const IrInstruction *ir, NumericBuiltinKind kind) {
    static const char *const names[] = {
        [BUILTIN_SUBGROUP_ADD]="simd_sum", [BUILTIN_SUBGROUP_MUL]="simd_product",
        [BUILTIN_SUBGROUP_MIN]="simd_min", [BUILTIN_SUBGROUP_MAX]="simd_max",
        [BUILTIN_SUBGROUP_AND]="simd_and", [BUILTIN_SUBGROUP_OR]="simd_or", [BUILTIN_SUBGROUP_XOR]="simd_xor",
        [BUILTIN_SUBGROUP_EXCLUSIVE_ADD]="simd_prefix_exclusive_sum", [BUILTIN_SUBGROUP_INCLUSIVE_ADD]="simd_prefix_inclusive_sum",
        [BUILTIN_SUBGROUP_EXCLUSIVE_MUL]="simd_prefix_exclusive_product", [BUILTIN_SUBGROUP_INCLUSIVE_MUL]="simd_prefix_inclusive_product",
        [BUILTIN_SUBGROUP_ALL]="simd_all", [BUILTIN_SUBGROUP_ANY]="simd_any",
        [BUILTIN_SUBGROUP_ELECT]="simd_is_first", [BUILTIN_SUBGROUP_BROADCAST]="simd_broadcast",
        [BUILTIN_SUBGROUP_BROADCAST_FIRST]="simd_broadcast_first", [BUILTIN_SUBGROUP_SHUFFLE]="simd_shuffle",
        [BUILTIN_SUBGROUP_SHUFFLE_XOR]="simd_shuffle_xor", [BUILTIN_SUBGROUP_SHUFFLE_UP]="simd_shuffle_up",
        [BUILTIN_SUBGROUP_SHUFFLE_DOWN]="simd_shuffle_down"
    };
    if (kind >= BUILTIN_QUAD_BROADCAST && kind <= BUILTIN_QUAD_SWAP_Y) {
        metal_text(e,kind == BUILTIN_QUAD_BROADCAST ? "quad_broadcast(" : "quad_shuffle_xor(");
        metal_value(e,ir->operands.scalar.left);
        metal_text(e,", ");
        if (kind == BUILTIN_QUAD_BROADCAST) metal_value(e,ir->operands.scalar.right);
        else metal_text(e,kind == BUILTIN_QUAD_SWAP_DIAGONAL ? "3u" : kind == BUILTIN_QUAD_SWAP_X ? "1u" : "2u");
        metal_text(e,")");
        return;
    }
    if (kind == BUILTIN_SUBGROUP_BALLOT) {
        /* Apple SIMD groups have at most 64 lanes; preserve both mask words. */
        metal_text(e,"uint4(uint(simd_ballot(");
        metal_value(e,ir->operands.scalar.left);
        metal_text(e,").get_mask()), uint(simd_ballot(");
        metal_value(e,ir->operands.scalar.left);
        metal_text(e,").get_mask() >> 32ul), 0u, 0u)");
        return;
    }
    const NumericBuiltin *builtin = builtin_kind(kind);
    if ((size_t)kind >= sizeof(names)/sizeof(*names) || names[kind] == NULL) {
        metal_error(e,"Metal subgroup operation has no mapping"); return;
    }
    metal_format(e,"%s(",names[kind]);
    if (builtin->arity != 0u) metal_value(e,ir->operands.scalar.left);
    if (builtin->arity == 2u) { metal_text(e,", "); metal_value(e,ir->operands.scalar.right); }
    metal_text(e,")");
}
