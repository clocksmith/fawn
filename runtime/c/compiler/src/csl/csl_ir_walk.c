// SPDX-License-Identifier: MIT
// See header for what is and is not ported.
#include "csl_ir_walk.h"
#include <string.h>
int csl_is_barrier_name(const char *name) {
    return name && (strcmp(name, "workgroupBarrier") == 0 || strcmp(name, "storageBarrier") == 0);
}
int csl_is_barrier_expr(CslExprKind kind) { return kind == CSL_EXPR_BARRIER_CALL; }
int csl_is_barrier_stmt(CslExprKind kind) { return csl_is_barrier_expr(kind); }
int csl_is_uniform_global_base(int is_uniform, int is_global) { return is_uniform && is_global; }
CslErr csl_walk_write(CslBuf *out, const char *text) { return csl_buf_puts(out, text); }
CslErr csl_walk_write_indent(CslBuf *out, unsigned depth) {
    unsigned i; CslErr e;
    if (!out) return CSL_ERR_INVALID;
    for (i = 0; i < depth; i++) { e = csl_buf_puts(out, "    "); if (e) return e; }
    return CSL_OK;
}
CslErr csl_walk_write_int(CslBuf *out, long long value) { return csl_buf_put_int(out, value); }
CslErr csl_emit_builtin_member_shim(CslBuf *out, const CslWalkConfig *cfg, CslBuiltin builtin, const char *member) {
    int lane;
    if (!out || !cfg || !member) return CSL_ERR_INVALID;
    lane = cfg->lane_mode;
    if (strcmp(member, "x") != 0) return CSL_ERR_UNSUPPORTED;
    switch (builtin) {
        case CSL_BUILTIN_LID:
            return csl_buf_puts(out, lane ? "lane_idx" : "0");
        case CSL_BUILTIN_GID:
            if (lane) return csl_buf_puts(out, "(@as(u32, pe_id) * @as(u32, wg_size_x) + lane_idx)");
            return csl_buf_puts(out, "pe_id");
        case CSL_BUILTIN_WID:
            return csl_buf_puts(out, "@as(u32, pe_id)");
        case CSL_BUILTIN_NUM_WORKGROUPS:
            return csl_buf_puts(out, "@as(u32, num_pes)");
        default: return CSL_ERR_UNSUPPORTED;
    }
}
CslErr csl_walk_stmt(void) { return CSL_ERR_UNSUPPORTED; }
CslErr csl_walk_expr(void) { return CSL_ERR_UNSUPPORTED; }
CslErr csl_walk_function_body(void) { return CSL_ERR_UNSUPPORTED; }
