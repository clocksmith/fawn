/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend_expr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *text) {
    size_t len = 0;
    char *copy = NULL;
    if (text == NULL) {
        return NULL;
    }
    len = strlen(text);
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

static char *concat3(const char *a, const char *b, const char *c) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t lc = strlen(c);
    char *out = (char *)malloc(la + lb + lc + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, a, la);
    memcpy(out + la, b, lb);
    memcpy(out + la + lb, c, lc + 1);
    return out;
}

static char *fmt_u64(uint64_t value) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)value);
    return dup_str(tmp);
}

static char *fmt_prefixed_u64(const char *prefix, uint64_t value) {
    char *digits = fmt_u64(value);
    char *out = NULL;
    if (digits == NULL) {
        return NULL;
    }
    out = concat3(prefix, digits, "");
    free(digits);
    return out;
}

static const TsirIrExpr *expr_at(const TsirIrFunction *func,
    uint32_t id) {
    if (func == NULL || id >= func->expr_count) {
        return NULL;
    }
    return &func->exprs[id];
}

static const TsirIrStmt *stmt_at(const TsirIrFunction *func,
    uint32_t id) {
    if (func == NULL || id >= func->stmt_count) {
        return NULL;
    }
    return &func->stmts[id];
}

/* True when expr is load(local_ref(induction)). */
static bool is_load_of_local(const TsirIrFunction *func,
    uint32_t expr_id, uint32_t induction) {
    const TsirIrExpr *node = expr_at(func, expr_id);
    const TsirIrExpr *inner = NULL;
    if (node == NULL || node->kind != TSIR_IR_EXPR_LOAD) {
        return false;
    }
    inner = expr_at(func, node->inner);
    return inner != NULL && inner->kind == TSIR_IR_EXPR_LOCAL_REF &&
        inner->ref_index == induction;
}

/* Split a binary comparison into (effective less/less_equal op,
 * name-side expr id). Mirrors Zig's canonical/mirror handling. */
static bool comparison_shape(const TsirIrFunction *func,
    uint32_t cond_id, uint32_t induction, bool *is_less_equal,
    uint32_t *name_expr_id) {
    const TsirIrExpr *cond = expr_at(func, cond_id);
    if (cond == NULL || cond->kind != TSIR_IR_EXPR_BINARY) {
        return false;
    }
    switch (cond->binary_op) {
        case TSIR_IR_BIN_LESS:
        case TSIR_IR_BIN_LESS_EQUAL:
            if (!is_load_of_local(func, cond->binary_lhs,
                induction)) {
                return false;
            }
            *is_less_equal =
                (cond->binary_op == TSIR_IR_BIN_LESS_EQUAL);
            *name_expr_id = cond->binary_rhs;
            return true;
        case TSIR_IR_BIN_GREATER:
        case TSIR_IR_BIN_GREATER_EQUAL:
            if (!is_load_of_local(func, cond->binary_rhs,
                induction)) {
                return false;
            }
            *is_less_equal =
                (cond->binary_op == TSIR_IR_BIN_GREATER_EQUAL);
            *name_expr_id = cond->binary_lhs;
            return true;
        default:
            return false;
    }
}

int tsir_expr_literal_upper_bound(const TsirIrFunction *func,
    bool has_cond, uint32_t cond_id, uint32_t induction,
    uint64_t *out) {
    const TsirIrExpr *cond = NULL;
    bool is_less_equal = false;
    uint32_t name_expr = 0;
    const TsirIrExpr *lit = NULL;
    if (!has_cond || out == NULL) {
        return 0;
    }
    cond = expr_at(func, cond_id);
    if (cond == NULL || cond->kind != TSIR_IR_EXPR_BINARY) {
        return 0;
    }
    if (!comparison_shape(func, cond_id, induction, &is_less_equal,
        &name_expr)) {
        return 0;
    }
    lit = expr_at(func, name_expr);
    if (lit == NULL || lit->kind != TSIR_IR_EXPR_INT_LIT) {
        return 0;
    }
    *out = lit->int_value + (is_less_equal ? 1u : 0u);
    return 1;
}

int tsir_expr_override_or_const_name(const TsirIrGlobal *global,
    char **out) {
    char tmp[64];
    if (global == NULL || out == NULL) {
        return 0;
    }
    if (global->class == TSIR_IR_GLOBAL_OVERRIDE) {
        if (global->has_override_id) {
            snprintf(tmp, sizeof(tmp), "override@id:%u",
                global->override_id);
            *out = dup_str(tmp);
        } else {
            *out = concat3("override:", global->name, "");
        }
    } else if (global->class == TSIR_IR_GLOBAL_CONST) {
        *out = concat3("const:", global->name, "");
    } else {
        return 0;
    }
    return (*out == NULL) ? -1 : 1;
}

bool tsir_expr_uniform_field_access(const TsirIrFunction *func,
    uint32_t expr_id, uint32_t *global_index_out,
    const char **field_name_out) {
    uint32_t cursor = expr_id;
    const TsirIrExpr *node = NULL;
    if (func == NULL) {
        return false;
    }
    for (;;) {
        node = expr_at(func, cursor);
        if (node == NULL) {
            return false;
        }
        if (node->kind == TSIR_IR_EXPR_LOAD) {
            cursor = node->inner;
            continue;
        }
        if (node->kind != TSIR_IR_EXPR_MEMBER) {
            return false;
        }
        {
            uint32_t base = node->member_base;
            const char *field = node->member_field_name;
            for (;;) {
                const TsirIrExpr *base_node = expr_at(func, base);
                if (base_node == NULL) {
                    return false;
                }
                if (base_node->kind == TSIR_IR_EXPR_LOAD) {
                    base = base_node->inner;
                    continue;
                }
                if (base_node->kind == TSIR_IR_EXPR_GLOBAL_REF) {
                    if (global_index_out != NULL) {
                        *global_index_out = base_node->ref_index;
                    }
                    if (field_name_out != NULL) {
                        *field_name_out = field;
                    }
                    return true;
                }
                return false;
            }
        }
    }
}

/* Resolve an expression to a bound-name string: uniform field
 * first, then override/const global. Returns 1/0/-1. */
static int resolve_bound_name(const TsirWgslModule *module,
    const TsirIrFunction *func, uint32_t expr_id, char **out) {
    uint32_t global_index = 0;
    const char *field = NULL;
    if (tsir_expr_uniform_field_access(func, expr_id, &global_index,
        &field)) {
        if (module != NULL && global_index < module->global_count) {
            const TsirIrGlobal *g = &module->globals[global_index];
            if (g->class == TSIR_IR_GLOBAL_VAR &&
                g->has_addr_space &&
                g->addr_space == TSIR_IR_SPACE_UNIFORM) {
                *out = concat3("uniform:", g->name, "");
                if (*out == NULL) {
                    return -1;
                }
                {
                    char *joined = concat3(*out, ".", field);
                    free(*out);
                    *out = joined;
                }
                return (*out == NULL) ? -1 : 1;
            }
        }
    }
    {
        uint32_t base = 0;
        if (tsir_ir_find_global_base(func, expr_id, &base) &&
            module != NULL && base < module->global_count) {
            return tsir_expr_override_or_const_name(
                &module->globals[base], out);
        }
    }
    return 0;
}

int tsir_expr_symbolic_upper_bound(const TsirWgslModule *module,
    const TsirIrFunction *func, bool has_cond, uint32_t cond_id,
    uint32_t induction, char **out) {
    bool is_less_equal = false;
    uint32_t name_expr = 0;
    int rc = 0;
    char *base = NULL;
    if (!has_cond || out == NULL) {
        return 0;
    }
    if (!comparison_shape(func, cond_id, induction, &is_less_equal,
        &name_expr)) {
        return 0;
    }
    rc = resolve_bound_name(module, func, name_expr, &base);
    if (rc != 1) {
        return rc;
    }
    if (is_less_equal) {
        char *joined = concat3(base, "+1", "");
        free(base);
        base = joined;
        if (base == NULL) {
            return -1;
        }
    }
    *out = base;
    return 1;
}

int tsir_expr_extract_step(const TsirWgslModule *module,
    const TsirIrFunction *func, bool has_cont, uint32_t cont_id,
    uint32_t induction, char **out) {
    const TsirIrStmt *cont = NULL;
    const char *prefix = "";
    uint32_t value_expr = 0;
    const TsirIrExpr *value_node = NULL;
    if (!has_cont || out == NULL) {
        return 0;
    }
    cont = stmt_at(func, cont_id);
    if (cont == NULL || cont->kind != TSIR_IR_STMT_ASSIGN) {
        return 0;
    }
    {
        const TsirIrExpr *lhs = expr_at(func, cont->assign_lhs);
        if (lhs == NULL || lhs->kind != TSIR_IR_EXPR_LOCAL_REF ||
            lhs->ref_index != induction) {
            return 0;
        }
    }
    value_expr = cont->assign_rhs;
    switch (cont->assign_op) {
        case TSIR_IR_ASSIGN_ADD:
            prefix = "";
            break;
        case TSIR_IR_ASSIGN_SUB:
            prefix = "-";
            break;
        case TSIR_IR_ASSIGN: {
            const TsirIrExpr *rhs = expr_at(func, cont->assign_rhs);
            if (rhs == NULL || rhs->kind != TSIR_IR_EXPR_BINARY) {
                return 0;
            }
            if (!is_load_of_local(func, rhs->binary_lhs,
                induction)) {
                return 0;
            }
            value_expr = rhs->binary_rhs;
            if (rhs->binary_op == TSIR_IR_BIN_ADD) {
                prefix = "";
            } else if (rhs->binary_op == TSIR_IR_BIN_SUB) {
                prefix = "-";
            } else {
                return 0;
            }
            break;
        }
        default:
            return 0;
    }
    value_node = expr_at(func, value_expr);
    if (value_node == NULL) {
        return 0;
    }
    if (value_node->kind == TSIR_IR_EXPR_INT_LIT) {
        *out = fmt_prefixed_u64(prefix, value_node->int_value);
        return (*out == NULL) ? -1 : 1;
    }
    {
        int rc = resolve_bound_name(module, func, value_expr, out);
        if (rc != 1) {
            return rc;
        }
        if (prefix[0] != '\0') {
            char *joined = concat3(prefix, *out, "");
            free(*out);
            *out = joined;
            if (*out == NULL) {
                return -1;
            }
        }
        return 1;
    }
}

int tsir_expr_extract_init_bound(const TsirWgslModule *module,
    const TsirIrFunction *func, bool has_init, uint32_t init_id,
    char **out) {
    const TsirIrExpr *node = NULL;
    if (!has_init || out == NULL) {
        return 0;
    }
    node = expr_at(func, init_id);
    if (node == NULL) {
        return 0;
    }
    if (node->kind == TSIR_IR_EXPR_INT_LIT) {
        *out = fmt_u64(node->int_value);
        return (*out == NULL) ? -1 : 1;
    }
    return resolve_bound_name(module, func, init_id, out);
}

TsirStepSign tsir_expr_detect_step_sign(const TsirIrFunction *func,
    bool has_cont, uint32_t cont_id, uint32_t induction) {
    const TsirIrStmt *cont = NULL;
    const TsirIrExpr *lhs = NULL;
    if (!has_cont) {
        return TSIR_STEP_UNKNOWN;
    }
    cont = stmt_at(func, cont_id);
    if (cont == NULL || cont->kind != TSIR_IR_STMT_ASSIGN) {
        return TSIR_STEP_UNKNOWN;
    }
    lhs = expr_at(func, cont->assign_lhs);
    if (lhs == NULL || lhs->kind != TSIR_IR_EXPR_LOCAL_REF ||
        lhs->ref_index != induction) {
        return TSIR_STEP_UNKNOWN;
    }
    switch (cont->assign_op) {
        case TSIR_IR_ASSIGN_ADD:
            return TSIR_STEP_POSITIVE;
        case TSIR_IR_ASSIGN_SUB:
            return TSIR_STEP_NEGATIVE;
        case TSIR_IR_ASSIGN: {
            const TsirIrExpr *rhs = expr_at(func, cont->assign_rhs);
            if (rhs == NULL || rhs->kind != TSIR_IR_EXPR_BINARY) {
                return TSIR_STEP_UNKNOWN;
            }
            if (!is_load_of_local(func, rhs->binary_lhs,
                induction)) {
                return TSIR_STEP_UNKNOWN;
            }
            if (rhs->binary_op == TSIR_IR_BIN_ADD) {
                return TSIR_STEP_POSITIVE;
            }
            if (rhs->binary_op == TSIR_IR_BIN_SUB) {
                return TSIR_STEP_NEGATIVE;
            }
            return TSIR_STEP_UNKNOWN;
        }
        default:
            return TSIR_STEP_UNKNOWN;
    }
}

bool tsir_expr_dispatch_axis_letter(const TsirIrFunction *func,
    bool has_init, uint32_t init_id, const char **letter_out) {
    uint32_t cursor = 0;
    const TsirIrExpr *node = NULL;
    const TsirIrExpr *member_node = NULL;
    uint32_t base = 0;
    if (!has_init || func == NULL) {
        return false;
    }
    cursor = init_id;
    for (;;) {
        node = expr_at(func, cursor);
        if (node == NULL) {
            return false;
        }
        if (node->kind == TSIR_IR_EXPR_LOAD) {
            cursor = node->inner;
            continue;
        }
        if (node->kind == TSIR_IR_EXPR_MEMBER) {
            break;
        }
        return false;
    }
    member_node = node;
    base = member_node->member_base;
    for (;;) {
        const TsirIrExpr *base_node = expr_at(func, base);
        if (base_node == NULL) {
            return false;
        }
        if (base_node->kind == TSIR_IR_EXPR_LOAD) {
            base = base_node->inner;
            continue;
        }
        if (base_node->kind == TSIR_IR_EXPR_PARAM_REF) {
            uint32_t pidx = base_node->ref_index;
            const TsirIrParam *param = NULL;
            if (pidx >= func->param_count) {
                return false;
            }
            param = &func->params[pidx];
            if (!param->has_io ||
                param->builtin !=
                    TSIR_IR_BUILTIN_GLOBAL_INVOCATION_ID) {
                return false;
            }
            if (letter_out != NULL) {
                *letter_out = member_node->member_field_name;
            }
            return true;
        }
        return false;
    }
}

/* One dispatch-guard statement: `if (i >= bound) { return; }` or
 * `if (i > bound) { return; }` with no else branch. */
static int dispatch_bound_from_guard(
    const TsirWgslModule *module, const TsirIrFunction *func,
    uint32_t guard_stmt_id, uint32_t dispatch_local, char **out) {
    const TsirIrStmt *stmt = stmt_at(func, guard_stmt_id);
    const TsirIrStmt *then_stmt = NULL;
    const TsirIrExpr *cond = NULL;
    const char *suffix = "";
    const TsirIrExpr *rhs = NULL;
    int rc = 0;
    char *base = NULL;
    if (stmt == NULL || stmt->kind != TSIR_IR_STMT_IF) {
        return 0;
    }
    if (stmt->has_if_else) {
        return 0;
    }
    then_stmt = stmt_at(func, stmt->if_then);
    if (then_stmt == NULL) {
        return 0;
    }
    if (then_stmt->kind == TSIR_IR_STMT_RETURN) {
        /* bare return: accepted */
    } else if (then_stmt->kind == TSIR_IR_STMT_BLOCK &&
        then_stmt->range_len == 1) {
        uint32_t inner_id = 0;
        const TsirIrStmt *inner = NULL;
        if (then_stmt->range_start >= func->stmt_child_count) {
            return 0;
        }
        inner_id = func->stmt_children[then_stmt->range_start];
        inner = stmt_at(func, inner_id);
        if (inner == NULL || inner->kind != TSIR_IR_STMT_RETURN) {
            return 0;
        }
    } else {
        return 0;
    }
    cond = expr_at(func, stmt->if_cond);
    if (cond == NULL || cond->kind != TSIR_IR_EXPR_BINARY) {
        return 0;
    }
    if (cond->binary_op != TSIR_IR_BIN_GREATER &&
        cond->binary_op != TSIR_IR_BIN_GREATER_EQUAL) {
        return 0;
    }
    if (!is_load_of_local(func, cond->binary_lhs, dispatch_local)) {
        return 0;
    }
    suffix = (cond->binary_op == TSIR_IR_BIN_GREATER) ? "+1" : "";
    rhs = expr_at(func, cond->binary_rhs);
    if (rhs == NULL) {
        return 0;
    }
    if (rhs->kind == TSIR_IR_EXPR_INT_LIT) {
        *out = fmt_u64(rhs->int_value);
        if (*out == NULL) {
            return -1;
        }
    } else {
        rc = resolve_bound_name(module, func, cond->binary_rhs,
            &base);
        if (rc != 1) {
            return rc;
        }
        *out = base;
    }
    if (suffix[0] != '\0') {
        char *joined = concat3(*out, suffix, "");
        free(*out);
        *out = joined;
        if (*out == NULL) {
            return -1;
        }
    }
    return 1;
}

int tsir_expr_scan_dispatch_guard(const TsirWgslModule *module,
    const TsirIrFunction *func, uint32_t range_start,
    uint32_t range_len, uint32_t start_pos, uint32_t dispatch_local,
    char **out) {
    uint32_t k = 0;
    if (func == NULL || out == NULL) {
        return 0;
    }
    for (k = start_pos + 1; k < range_len; k++) {
        uint32_t child_id = 0;
        const TsirIrStmt *stmt = NULL;
        int rc = 0;
        if (range_start + k >= func->stmt_child_count) {
            return 0;
        }
        child_id = func->stmt_children[range_start + k];
        stmt = stmt_at(func, child_id);
        if (stmt == NULL) {
            return 0;
        }
        if (stmt->kind == TSIR_IR_STMT_LOCAL_DECL) {
            continue;
        }
        if (stmt->kind != TSIR_IR_STMT_IF) {
            return 0;
        }
        rc = dispatch_bound_from_guard(module, func, child_id,
            dispatch_local, out);
        if (rc == 1 || rc == -1) {
            return rc;
        }
        if (stmt->has_if_else) {
            return 0;
        }
        {
            const TsirIrStmt *then_stmt =
                stmt_at(func, stmt->if_then);
            bool early_return = false;
            if (then_stmt != NULL &&
                then_stmt->kind == TSIR_IR_STMT_RETURN) {
                early_return = true;
            } else if (then_stmt != NULL &&
                then_stmt->kind == TSIR_IR_STMT_BLOCK &&
                then_stmt->range_len == 1 &&
                then_stmt->range_start < func->stmt_child_count) {
                const TsirIrStmt *inner = stmt_at(func,
                    func->stmt_children[then_stmt->range_start]);
                early_return = inner != NULL &&
                    inner->kind == TSIR_IR_STMT_RETURN;
            }
            if (!early_return) {
                return 0;
            }
        }
    }
    return 0;
}
