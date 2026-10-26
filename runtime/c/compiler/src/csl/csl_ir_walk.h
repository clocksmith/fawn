// SPDX-License-Identifier: MIT
// emit_csl_ir_walk.zig: shared IR walker for CSL pattern emitters.
// Ported: barrier predicates (over descriptor expression kinds, since
// there is no C IR), indent/integer writers, lane-mode builtin shim
// text, uniform-global-base predicate over descriptor kinds.
// NOT ported: the full statement/expression walker (stmt/expr/
// functionBody/writeType/helperFunctions/comptimeExports over ir.*):
// those return CSL_ERR_UNSUPPORTED. WalkConfig mirrors Zig: skip_barriers
// elides workgroupBarrier/storageBarrier; lane_mode switches the shim
// to lane-sequential substitutions; runtime_array_size is "chunk_size"
// (element-wise) or "hidden_size" (reduction).
#ifndef CSL_IR_WALK_H
#define CSL_IR_WALK_H
#include "csl_kernel.h"
typedef enum { CSL_EXPR_OTHER, CSL_EXPR_BARRIER_CALL, CSL_EXPR_BUILTIN_CALL } CslExprKind;
typedef enum { CSL_BUILTIN_NONE, CSL_BUILTIN_LID, CSL_BUILTIN_GID, CSL_BUILTIN_WID, CSL_BUILTIN_NUM_WORKGROUPS } CslBuiltin;
typedef struct {
    int skip_barriers; int lane_mode; const char *runtime_array_size;
} CslWalkConfig;
int csl_is_barrier_name(const char *name);
int csl_is_barrier_expr(CslExprKind kind);
int csl_is_barrier_stmt(CslExprKind kind);
int csl_is_uniform_global_base(int is_uniform, int is_global);
CslErr csl_walk_write(CslBuf *out, const char *text);
CslErr csl_walk_write_indent(CslBuf *out, unsigned depth);
CslErr csl_walk_write_int(CslBuf *out, long long value);
/* Shim text for a builtin member access, following Zig's mapping. */
CslErr csl_emit_builtin_member_shim(CslBuf *out, const CslWalkConfig *cfg, CslBuiltin builtin, const char *member);
CslErr csl_walk_stmt(void);
CslErr csl_walk_expr(void);
CslErr csl_walk_function_body(void);
#endif
