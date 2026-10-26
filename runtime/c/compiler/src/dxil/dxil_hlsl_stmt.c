/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"

#include <string.h>

static const char *assign_text(DxilIrAssignOp op) {
    switch (op) {
        case DXIL_IR_ASSIGN_ASSIGN:
            return "=";
        case DXIL_IR_ASSIGN_ADD:
            return "+=";
        case DXIL_IR_ASSIGN_SUB:
            return "-=";
        case DXIL_IR_ASSIGN_MUL:
            return "*=";
        case DXIL_IR_ASSIGN_DIV:
            return "/=";
        case DXIL_IR_ASSIGN_REM:
            return "%=";
        case DXIL_IR_ASSIGN_BIT_AND:
            return "&=";
        case DXIL_IR_ASSIGN_BIT_OR:
            return "|=";
        case DXIL_IR_ASSIGN_BIT_XOR:
            return "^=";
        case DXIL_IR_ASSIGN_SHIFT_LEFT:
            return "<<=";
        case DXIL_IR_ASSIGN_SHIFT_RIGHT:
            return ">>=";
    }
    return "=";
}

DxilStatus dxil_hlsl_emit_block_contents(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrStmtId stmt_id) {
    const DxilIrStmt *stmt = NULL;
    uint32_t i = 0;
    DxilStatus status = DXIL_OK;
    if (stmt_id >= function->stmt_count) {
        return DXIL_INVALID_IR;
    }
    stmt = &function->stmts[stmt_id];
    if (stmt->kind != DXIL_IR_STMT_BLOCK) {
        return dxil_hlsl_emit_stmt(emitter, function, stmt_id);
    }
    for (i = 0; i < stmt->range.len; i++) {
        status = dxil_hlsl_emit_stmt(emitter, function,
            function->stmt_children[stmt->range.start + i]);
        if (status != DXIL_OK) {
            return status;
        }
    }
    return DXIL_OK;
}

DxilStatus dxil_hlsl_emit_stmt(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrStmtId stmt_id) {
    const DxilIrStmt *stmt = NULL;
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    if (emitter->depth > 512) {
        return DXIL_INVALID_IR;
    }
    if (stmt_id >= function->stmt_count) {
        return DXIL_INVALID_IR;
    }
    emitter->depth += 1;
    stmt = &function->stmts[stmt_id];
    switch (stmt->kind) {
        case DXIL_IR_STMT_BLOCK:
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "{\n");
            if (status != DXIL_OK) {
                break;
            }
            emitter->indent += 4;
            status = dxil_hlsl_emit_block_contents(emitter, function, stmt_id);
            if (status != DXIL_OK) {
                break;
            }
            emitter->indent -= 4;
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "}\n");
            break;
        case DXIL_IR_STMT_LOCAL_DECL: {
            const DxilIrLocal *local = NULL;
            DxilIrType local_type;
            if (stmt->local >= function->local_count) {
                status = DXIL_INVALID_IR;
                break;
            }
            local = &function->locals[stmt->local];
            if (dxil_ir_type_get(emitter->module, local->ty,
                    &local_type) == DXIL_OK
                && local_type.kind == DXIL_IR_TYPE_REF) {
                status = DXIL_INVALID_IR;
                break;
            }
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->is_const) {
                status = dxil_hlsl_write_text(emitter, "const ");
                if (status != DXIL_OK) {
                    break;
                }
            }
            status = dxil_hlsl_emit_typed_name(emitter, local->ty, local->name);
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->has_initializer) {
                status = dxil_hlsl_write_text(emitter, " = ");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_emit_expr(emitter, function,
                    stmt->initializer);
                if (status != DXIL_OK) {
                    break;
                }
            }
            status = dxil_hlsl_write_text(emitter, ";\n");
            break;
        }
        case DXIL_IR_STMT_EXPR:
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, stmt->expr);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ";\n");
            break;
        case DXIL_IR_STMT_ASSIGN:
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, stmt->assign_lhs);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, " ");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, assign_text(stmt->assign_op));
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, " ");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, stmt->assign_rhs);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ";\n");
            break;
        case DXIL_IR_STMT_RETURN:
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "return");
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->has_return_value) {
                status = dxil_hlsl_write_text(emitter, " ");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_emit_expr(emitter, function,
                    stmt->return_value);
                if (status != DXIL_OK) {
                    break;
                }
            }
            status = dxil_hlsl_write_text(emitter, ";\n");
            break;
        case DXIL_IR_STMT_IF:
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "if (");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, stmt->cond);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ") {\n");
            if (status != DXIL_OK) {
                break;
            }
            emitter->indent += 4;
            status = dxil_hlsl_emit_block_contents(emitter, function,
                stmt->then_block);
            if (status != DXIL_OK) {
                break;
            }
            emitter->indent -= 4;
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "}");
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->has_else_block) {
                status = dxil_hlsl_write_text(emitter, " else {\n");
                if (status != DXIL_OK) {
                    break;
                }
                emitter->indent += 4;
                status = dxil_hlsl_emit_block_contents(emitter, function,
                    stmt->else_block);
                if (status != DXIL_OK) {
                    break;
                }
                emitter->indent -= 4;
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "}\n");
            } else {
                status = dxil_hlsl_write_text(emitter, "\n");
            }
            break;
        case DXIL_IR_STMT_LOOP: {
            bool prev_loop_continuing_has =
                emitter->loop_continuing_has;
            DxilIrStmtId prev_loop_continuing =
                emitter->loop_continuing;
            bool prev_loop_break_has = emitter->loop_break_has;
            DxilIrStmtId prev_loop_break = emitter->loop_break;
            bool prev_continuing_break_has =
                emitter->continuing_break_has;
            DxilIrStmtId prev_continuing_break =
                emitter->continuing_break;
            if (stmt->has_init || stmt->has_continuing) {
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "{\n");
                if (status != DXIL_OK) {
                    break;
                }
                emitter->indent += 4;
            }
            if (stmt->has_init) {
                status = dxil_hlsl_emit_stmt(emitter, function, stmt->init);
                if (status != DXIL_OK) {
                    break;
                }
            }
            if (stmt->has_continuing) {
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter,
                    "bool __doe_loop_break_");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_u32(emitter, stmt_id);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, " = false;\n");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
            } else {
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
            }
            status = dxil_hlsl_write_text(emitter, "while (");
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->has_continuing) {
                status = dxil_hlsl_write_text(emitter, "!__doe_loop_break_");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_u32(emitter, stmt_id);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, " && (");
                if (status != DXIL_OK) {
                    break;
                }
            }
            if (stmt->has_cond) {
                status = dxil_hlsl_emit_expr(emitter, function, stmt->cond);
            } else {
                status = dxil_hlsl_write_text(emitter, "true");
            }
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->has_continuing) {
                status = dxil_hlsl_write_text(emitter, ")");
                if (status != DXIL_OK) {
                    break;
                }
            }
            status = dxil_hlsl_write_text(emitter, ") {\n");
            if (status != DXIL_OK) {
                break;
            }
            emitter->indent += 4;
            emitter->loop_break_has = stmt->has_continuing;
            emitter->loop_break = stmt_id;
            emitter->continuing_break_has = false;
            emitter->loop_continuing_has = stmt->has_continuing;
            emitter->loop_continuing = stmt->continuing;
            status = dxil_hlsl_emit_block_contents(emitter, function,
                stmt->body);
            emitter->loop_continuing_has = false;
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->has_continuing) {
                status = dxil_hlsl_emit_stmt(emitter, function,
                    stmt->continuing);
                if (status != DXIL_OK) {
                    break;
                }
            }
            emitter->indent -= 4;
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "}\n");
            if (status != DXIL_OK) {
                break;
            }
            if (stmt->has_init || stmt->has_continuing) {
                emitter->indent -= 4;
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "}\n");
                if (status != DXIL_OK) {
                    break;
                }
            }
            emitter->loop_continuing_has = prev_loop_continuing_has;
            emitter->loop_continuing = prev_loop_continuing;
            emitter->loop_break_has = prev_loop_break_has;
            emitter->loop_break = prev_loop_break;
            emitter->continuing_break_has = prev_continuing_break_has;
            emitter->continuing_break = prev_continuing_break;
            break;
        }
        case DXIL_IR_STMT_SWITCH: {
            bool prev_continuing_break_has =
                emitter->continuing_break_has;
            DxilIrStmtId prev_continuing_break =
                emitter->continuing_break;
            emitter->continuing_break_has = false;
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "switch (");
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_emit_expr(emitter, function, stmt->switch_expr);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, ") {\n");
            if (status != DXIL_OK) {
                break;
            }
            emitter->indent += 4;
            for (i = 0; i < stmt->cases.len; i++) {
                const DxilIrSwitchCase *switch_case =
                    &function->switch_cases[stmt->cases.start + i];
                uint32_t si = 0;
                if (switch_case->is_default) {
                    status = dxil_hlsl_write_indent(emitter);
                    if (status != DXIL_OK) {
                        break;
                    }
                    status = dxil_hlsl_write_text(emitter, "default:\n");
                    if (status != DXIL_OK) {
                        break;
                    }
                } else {
                    for (si = 0; si < switch_case->selectors.len;
                        si++) {
                        status = dxil_hlsl_write_indent(emitter);
                        if (status != DXIL_OK) {
                            break;
                        }
                        status = dxil_hlsl_write_text(emitter, "case ");
                        if (status != DXIL_OK) {
                            break;
                        }
                        status = dxil_hlsl_emit_expr(emitter, function,
                            function->switch_selectors[
                                switch_case->selectors.start + si]);
                        if (status != DXIL_OK) {
                            break;
                        }
                        status = dxil_hlsl_write_text(emitter, ":\n");
                        if (status != DXIL_OK) {
                            break;
                        }
                    }
                    if (status != DXIL_OK) {
                        break;
                    }
                }
                emitter->indent += 4;
                status = dxil_hlsl_emit_stmt(emitter, function,
                    switch_case->body);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "break;\n");
                if (status != DXIL_OK) {
                    break;
                }
                emitter->indent -= 4;
            }
            if (status != DXIL_OK) {
                break;
            }
            emitter->indent -= 4;
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "}\n");
            emitter->continuing_break_has = prev_continuing_break_has;
            emitter->continuing_break = prev_continuing_break;
            break;
        }
        case DXIL_IR_STMT_BREAK:
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            if (emitter->continuing_break_has) {
                status = dxil_hlsl_write_text(emitter, "__doe_loop_break_");
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_u32(emitter,
                    emitter->continuing_break);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, " = true;\n");
            } else {
                status = dxil_hlsl_write_text(emitter, "break;\n");
            }
            break;
        case DXIL_IR_STMT_CONTINUE: {
            bool prev_continuing_break_has =
                emitter->continuing_break_has;
            DxilIrStmtId prev_continuing_break =
                emitter->continuing_break;
            DxilIrStmtId continuing = 0;
            if (emitter->loop_continuing_has) {
                continuing = emitter->loop_continuing;
                emitter->continuing_break_has =
                    emitter->loop_break_has;
                emitter->continuing_break = emitter->loop_break;
                emitter->loop_continuing_has = false;
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "{\n");
                if (status != DXIL_OK) {
                    break;
                }
                emitter->indent += 4;
                status = dxil_hlsl_emit_stmt(emitter, function, continuing);
                if (status != DXIL_OK) {
                    break;
                }
                emitter->indent -= 4;
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    break;
                }
                status = dxil_hlsl_write_text(emitter, "}\n");
                if (status != DXIL_OK) {
                    break;
                }
                emitter->continuing_break_has =
                    prev_continuing_break_has;
                emitter->continuing_break = prev_continuing_break;
                emitter->loop_continuing_has = true;
                emitter->loop_continuing = continuing;
            }
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "continue;\n");
            break;
        }
        case DXIL_IR_STMT_DISCARD:
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                break;
            }
            status = dxil_hlsl_write_text(emitter, "discard;\n");
            break;
    }
    emitter->depth -= 1;
    return status;
}

