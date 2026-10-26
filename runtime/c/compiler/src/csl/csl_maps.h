// SPDX-License-Identifier: MIT
// WGSL to CSL name and type mappings mirroring emit_csl_maps.zig.
#ifndef CSL_MAPS_H
#define CSL_MAPS_H

typedef enum {
    CSL_UNARY_NEG,
    CSL_UNARY_NOT,
    CSL_UNARY_BIT_NOT
} CslUnaryOp;

typedef enum {
    CSL_BIN_ADD, CSL_BIN_SUB, CSL_BIN_MUL, CSL_BIN_DIV, CSL_BIN_REM,
    CSL_BIN_BIT_AND, CSL_BIN_BIT_OR, CSL_BIN_BIT_XOR,
    CSL_BIN_SHIFT_LEFT, CSL_BIN_SHIFT_RIGHT,
    CSL_BIN_EQUAL, CSL_BIN_NOT_EQUAL, CSL_BIN_LESS, CSL_BIN_LESS_EQUAL,
    CSL_BIN_GREATER, CSL_BIN_GREATER_EQUAL,
    CSL_BIN_LOGICAL_AND, CSL_BIN_LOGICAL_OR
} CslBinaryOp;

typedef enum {
    CSL_ASSIGN, CSL_ASSIGN_ADD, CSL_ASSIGN_SUB, CSL_ASSIGN_MUL,
    CSL_ASSIGN_DIV, CSL_ASSIGN_REM, CSL_ASSIGN_BIT_AND, CSL_ASSIGN_BIT_OR,
    CSL_ASSIGN_BIT_XOR, CSL_ASSIGN_SHIFT_LEFT, CSL_ASSIGN_SHIFT_RIGHT
} CslAssignOp;

const char *csl_unary_op_text(CslUnaryOp op);
const char *csl_binary_op_text(CslBinaryOp op);
const char *csl_assign_op_text(CslAssignOp op);
/* CSL math library equivalent, or NULL when the builtin needs special
 * handling (inline expansion, DSD op, etc.). */
const char *csl_math_builtin(const char *name);
int csl_needs_inline_expansion(const char *name);
int csl_is_unsupported_builtin(const char *name);

#endif
