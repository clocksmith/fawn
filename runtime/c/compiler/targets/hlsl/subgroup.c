/* SPDX-License-Identifier: MIT */
#include "private.h"
#include "../../src/builtin.h"
void hlsl_subgroup(HlslEmitter *e, const IrInstruction *ir, NumericBuiltinKind kind) {
    const NumericBuiltin *builtin = builtin_kind(kind);
    e->uses_subgroups = true;
    const char *name = kind == BUILTIN_QUAD_BROADCAST ? "QuadReadLaneAt" :
        kind == BUILTIN_QUAD_SWAP_DIAGONAL ? "QuadReadAcrossDiagonal" :
        kind == BUILTIN_QUAD_SWAP_X ? "QuadReadAcrossX" : kind == BUILTIN_QUAD_SWAP_Y ? "QuadReadAcrossY" :
        kind == BUILTIN_SUBGROUP_ALL ? "WaveActiveAllTrue" :
        kind == BUILTIN_SUBGROUP_ANY ? "WaveActiveAnyTrue" :
        kind == BUILTIN_SUBGROUP_BALLOT ? "WaveActiveBallot" :
        kind == BUILTIN_SUBGROUP_ELECT ? "WaveIsFirstLane" :
        kind == BUILTIN_SUBGROUP_BROADCAST_FIRST ? "WaveReadLaneFirst" : "WaveReadLaneAt";
    hlsl_format(e,"%s(",name);
    if (builtin->arity != 0u) hlsl_value(e,ir->operands.scalar.left);
    if (builtin->arity == 2u) {
        hlsl_text(e,", ");
        if (kind == BUILTIN_SUBGROUP_SHUFFLE_XOR || kind == BUILTIN_SUBGROUP_SHUFFLE_UP || kind == BUILTIN_SUBGROUP_SHUFFLE_DOWN)
            hlsl_format(e,"(WaveGetLaneIndex() %s ",kind == BUILTIN_SUBGROUP_SHUFFLE_XOR ? "^" : kind == BUILTIN_SUBGROUP_SHUFFLE_UP ? "-" : "+");
        hlsl_value(e,ir->operands.scalar.right);
        if (kind == BUILTIN_SUBGROUP_SHUFFLE_XOR || kind == BUILTIN_SUBGROUP_SHUFFLE_UP || kind == BUILTIN_SUBGROUP_SHUFFLE_DOWN) hlsl_text(e,")");
    }
    hlsl_text(e,")");
}
