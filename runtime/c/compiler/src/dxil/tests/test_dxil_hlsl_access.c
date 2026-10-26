/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
/* Caller-built IR controls. Text checks are not DXC/device qualification. */
#include "dxil_hlsl.h"
#include "test_util.h"

static DxilIrModule module;
static char output[65536];
static size_t written;

enum { T_VOID, T_U32, T_ARRAY, T_POINTER };
static DxilIrFunction *base(bool runtime) {
    dxil_ir_init(&module);
    module.type_count = 4u;
    module.types[T_VOID] = (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_VOID};
    module.types[T_U32] = (DxilIrType){.kind = DXIL_IR_TYPE_SCALAR, .scalar = DXIL_IR_SCALAR_U32};
    module.types[T_ARRAY] = (DxilIrType){.kind = DXIL_IR_TYPE_ARRAY, .elem = T_U32, .has_len = !runtime, .array_len = 4u};
    module.types[T_POINTER] = (DxilIrType){.kind = DXIL_IR_TYPE_REF, .elem = T_U32,
        .addr_space = DXIL_IR_ADDR_STORAGE, .access = DXIL_IR_ACCESS_READ_WRITE};
    module.global_count = 1u;
    module.globals[0] = (DxilIrGlobal){.name = "buf", .ty = T_ARRAY, .class = DXIL_IR_GLOBAL_VAR,
        .has_addr_space = true, .addr_space = DXIL_IR_ADDR_STORAGE,
        .has_access = true, .access = DXIL_IR_ACCESS_READ_WRITE, .has_binding = true};
    module.function_count = 1u;
    DxilIrFunction *function = &module.functions[0];
    function->name = "main"; function->return_type = T_VOID;
    function->has_stage = true; function->stage = DXIL_IR_STAGE_COMPUTE;
    function->workgroup_size[0] = function->workgroup_size[1] = function->workgroup_size[2] = 1u;
    function->param_count = 1u;
    function->params[0] = (DxilIrParam){.name = "index", .ty = T_U32, .has_io = true,
        .io.builtin = DXIL_IR_BUILTIN_LOCAL_INVOCATION_INDEX};
    function->exprs[0] = (DxilIrExpr){.kind = DXIL_IR_EXPR_GLOBAL_REF, .ty = T_ARRAY,
        .category = DXIL_IR_EXPR_REF, .index = 0u};
    function->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_PARAM_REF, .ty = T_U32, .index = 0u};
    function->expr_count = 2u;
    return function;
}
static bool emit(void) {
    memset(output, 0, sizeof(output)); written = 0u;
    DxilStatus status = dxil_hlsl_emit(&module, output, sizeof(output), &written);
    CHECK_MSG(status == DXIL_OK, "low-level HLSL failed: %s", dxil_status_name(status));
    return status == DXIL_OK;
}
static void runtime_write(void) {
    DxilIrFunction *function = base(true);
    function->exprs[2] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CALL, .ty = T_U32,
        .name = "arrayLength", .call_kind = DXIL_IR_CALL_BUILTIN, .args = {0u, 1u}};
    function->exprs[3] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 1u};
    function->exprs[4] = (DxilIrExpr){.kind = DXIL_IR_EXPR_BINARY, .ty = T_U32,
        .binary_op = DXIL_IR_BINARY_SUB, .lhs = 2u, .rhs = 3u};
    function->exprs[5] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CALL, .ty = T_U32,
        .name = "min", .call_kind = DXIL_IR_CALL_BUILTIN, .args = {1u, 2u}, .robustness_generated = true};
    function->exprs[6] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INDEX, .ty = T_U32,
        .category = DXIL_IR_EXPR_REF, .base = 0u, .index_expr = 5u};
    function->exprs[7] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 9u};
    function->expr_count = 8u;
    function->expr_args[0] = 0u; function->expr_args[1] = 1u; function->expr_args[2] = 4u;
    function->expr_arg_count = 3u;
    function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 6u, .assign_rhs = 7u};
    function->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 1u}};
    function->stmt_count = 2u; function->root_stmt = 1u;
    function->stmt_children[0] = 0u; function->stmt_child_count = 1u;
    if (emit()) {
        CHECK(strstr(output, "buf.GetDimensions(count, stride);") != NULL);
        CHECK(strstr(output, "const uint doe_store_expr_0_1 = index;") != NULL);
        CHECK(strstr(output, "const uint doe_store_expr_0_2 = doe_arrayLength_buf();") != NULL);
        CHECK(strstr(output, "const uint doe_store_expr_0_5 = min(doe_store_expr_0_1, (doe_store_expr_0_2 - 1));") != NULL);
        CHECK(strstr(output, "if (doe_store_valid_0) {") != NULL);
        CHECK(strstr(output, "buf[doe_store_expr_0_5] = doe_store_rhs_0;") != NULL);
    }
    uint32_t unclamped = 99u, length = 99u;
    CHECK(dxil_ir_runtime_array_write_guard(function, 6u, &unclamped, &length));
    CHECK(unclamped == 1u && length == 2u);
    function->exprs[5].args.start = UINT32_MAX;
    unclamped = length = 99u;
    CHECK(!dxil_ir_runtime_array_write_guard(function, 6u, &unclamped, &length));
    CHECK(unclamped == 99u && length == 99u);
    function->exprs[5].args.start = 1u;
    CHECK(!dxil_ir_runtime_array_write_guard(function, 6u, NULL, &length));
    function->exprs[5].robustness_generated = false;
    if (emit()) CHECK(strstr(output, "if (index <") == NULL);
    function->exprs[5].robustness_generated = true;
    module.type_count = 5u;
    module.types[4] = (DxilIrType){.kind = DXIL_IR_TYPE_STRUCT, .struct_id = 0u};
    module.struct_count = 1u;
    module.structs[0].name = "Data"; module.structs[0].field_count = 2u;
    module.structs[0].fields[0] = (DxilIrStructField){.name = "header", .ty = T_U32};
    module.structs[0].fields[1] = (DxilIrStructField){.name = "tail", .ty = T_ARRAY};
    module.globals[0].ty = 4u; function->exprs[0].ty = 4u;
    function->exprs[8] = (DxilIrExpr){.kind = DXIL_IR_EXPR_MEMBER, .ty = T_ARRAY,
        .category = DXIL_IR_EXPR_REF, .base = 0u, .field_index = 1u, .field_name = "tail"};
    function->expr_count = 9u; function->expr_args[0] = 8u; function->exprs[6].base = 8u;
    if (emit()) {
        CHECK(strstr(output, "uint doe_arrayLength_buf_tail()") != NULL);
        CHECK(strstr(output, "return (stride - 4) / 4;") != NULL);
        CHECK(strstr(output, "const uint doe_store_expr_0_2 = doe_arrayLength_buf_tail();") != NULL);
    }
}
static void runtime_pointer_snapshot(void) {
    /* Extend the hand-built structured-tail fixture above. A formerly invalid
     * index becomes valid after pointer binding; the store must still consult
     * the immutable validity snapshot, not the changed parameter. */
    DxilIrFunction *function = &module.functions[0];
    function->local_count = 1u;
    function->locals[0] = (DxilIrLocal){.name = "pointer", .ty = T_POINTER};
    function->exprs[9] = (DxilIrExpr){.kind = DXIL_IR_EXPR_ADDRESS_OF, .ty = T_POINTER, .inner = 6u};
    function->exprs[10] = (DxilIrExpr){.kind = DXIL_IR_EXPR_LOCAL_REF, .ty = T_POINTER, .index = 0u};
    function->exprs[11] = (DxilIrExpr){.kind = DXIL_IR_EXPR_DEREF, .ty = T_U32, .inner = 10u};
    function->exprs[12] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 0u};
    function->exprs[13] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 999u};
    function->expr_count = 14u;
    function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 1u, .assign_rhs = 13u};
    function->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_LOCAL_DECL, .local = 0u,
        .is_const = true, .has_initializer = true, .initializer = 9u};
    function->stmts[2] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 1u, .assign_rhs = 12u};
    function->stmts[3] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 11u, .assign_rhs = 7u};
    function->stmts[4] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 4u}};
    function->stmt_count = 5u; function->root_stmt = 4u; function->stmt_child_count = 4u;
    for (uint32_t i = 0u; i < 4u; ++i) function->stmt_children[i] = i;
    if (emit()) {
        const char *invalid = strstr(output, "index = 999;");
        const char *raw = strstr(output, "const uint doe_pointer_raw_6 = index;");
        const char *validity = strstr(output, "const bool doe_pointer_valid_6 = doe_pointer_raw_6 < doe_pointer_length_6;");
        const char *mutation = strstr(output, "index = 0;");
        const char *guard = strstr(output, "if (doe_pointer_valid_6) {");
        CHECK(invalid != NULL && raw != NULL && validity != NULL && mutation != NULL && guard != NULL);
        CHECK(invalid != NULL && raw != NULL && validity != NULL && mutation != NULL && guard != NULL &&
            invalid < raw && raw < validity && validity < mutation && mutation < guard);
        CHECK(strstr(output, "const uint doe_pointer_length_6 = doe_arrayLength_buf_tail();") != NULL);
        CHECK(strstr(output, "const uint doe_pointer_index_6 = min(doe_pointer_raw_6, (doe_pointer_length_6 - 1));") != NULL);
        CHECK(strstr(output, "buf.tail[doe_pointer_index_6] = doe_store_rhs_3;") != NULL);
        CHECK(strstr(output, "if (index <") == NULL);
    }
}
static size_t occurrences(const char *text, const char *needle) {
    size_t count = 0u;
    for (const char *cursor = text; (cursor = strstr(cursor, needle)) != NULL; cursor += strlen(needle)) ++count;
    return count;
}
static void effect_function(uint32_t id, const char *name, uint32_t value) {
    DxilIrFunction *function = &module.functions[id];
    function->name = name; function->return_type = T_U32;
    function->exprs[0] = (DxilIrExpr){.kind = DXIL_IR_EXPR_GLOBAL_REF, .ty = T_U32, .index = 1u};
    function->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 1u};
    function->exprs[2] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = value};
    function->expr_count = 3u;
    function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_op = DXIL_IR_ASSIGN_ADD,
        .assign_lhs = 0u, .assign_rhs = 1u};
    function->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_RETURN, .has_return_value = true, .return_value = 2u};
    function->stmts[2] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 2u}};
    function->stmt_count = 3u; function->root_stmt = 2u; function->stmt_child_count = 2u;
    function->stmt_children[0] = 0u; function->stmt_children[1] = 1u;
}
static void store_effects(void) {
    /* Reuse the existing typed runtime clamp graph, with side-effectful user
     * calls in both index and RHS. No text assertion substitutes for DXC. */
    DxilIrFunction *function = &module.functions[0];
    function->local_count = 0u;
    function->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CALL, .ty = T_U32,
        .name = "next_index", .call_kind = DXIL_IR_CALL_USER};
    function->exprs[7] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CALL, .ty = T_U32,
        .name = "next_value", .call_kind = DXIL_IR_CALL_USER};
    function->expr_count = 9u;
    function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 6u, .assign_rhs = 7u};
    function->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 1u}};
    function->stmt_count = 2u; function->root_stmt = 1u; function->stmt_child_count = 1u; function->stmt_children[0] = 0u;
    module.global_count = 2u;
    module.globals[1] = (DxilIrGlobal){.name = "effects", .ty = T_U32, .class = DXIL_IR_GLOBAL_VAR,
        .has_addr_space = true, .addr_space = DXIL_IR_ADDR_PRIVATE, .has_initializer = true, .init_kind = DXIL_IR_CONST_INT};
    module.function_count = 3u;
    effect_function(1u, "next_index", 999u); effect_function(2u, "next_value", 9u);
    if (emit()) {
        const char *index = strstr(output, " = next_index();");
        const char *rhs = strstr(output, " = next_value();");
        const char *guard = strstr(output, "if (doe_store_valid_0) {");
        CHECK(occurrences(output, " = next_index();") == 1u);
        CHECK(occurrences(output, " = next_value();") == 1u);
        CHECK(index != NULL && rhs != NULL && guard != NULL && index < rhs && rhs < guard);
    }
    function->stmts[0].assign_op = DXIL_IR_ASSIGN_ADD;
    if (emit()) {
        const char *load = strstr(output, "if (doe_store_valid_0) doe_store_old_0 = buf.tail[doe_store_expr_0_5];");
        const char *rhs = strstr(output, " = next_value();");
        CHECK(load != NULL && rhs != NULL && load < rhs);
        CHECK(strstr(output, "buf.tail[doe_store_expr_0_5] = doe_store_old_0 + doe_store_rhs_0;") != NULL);
        CHECK(occurrences(output, " = next_index();") == 1u && occurrences(output, " = next_value();") == 1u);
    }
    function->stmts[0].assign_op = DXIL_IR_ASSIGN_ASSIGN;
    function->stmts[0].assign_rhs = 1u;
    if (emit()) {
        CHECK(occurrences(output, " = next_index();") == 2u);
        CHECK(strstr(output, "const uint doe_store_rhs_0 = next_index();") != NULL);
    }
}
static void pointer_snapshot(void) {
    DxilIrFunction *function = base(false);
    function->local_count = 2u;
    function->locals[0] = (DxilIrLocal){.name = "pointer", .ty = T_POINTER};
    function->locals[1] = (DxilIrLocal){.name = "unsigned", .ty = T_U32};
    function->exprs[2] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INDEX, .ty = T_U32,
        .category = DXIL_IR_EXPR_REF, .base = 0u, .index_expr = 1u};
    function->exprs[3] = (DxilIrExpr){.kind = DXIL_IR_EXPR_ADDRESS_OF, .ty = T_POINTER, .inner = 2u};
    function->exprs[4] = (DxilIrExpr){.kind = DXIL_IR_EXPR_LOCAL_REF, .ty = T_POINTER, .index = 0u};
    function->exprs[5] = (DxilIrExpr){.kind = DXIL_IR_EXPR_DEREF, .ty = T_U32, .inner = 4u};
    function->exprs[6] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 9u};
    function->exprs[7] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 2u};
    function->exprs[8] = (DxilIrExpr){.kind = DXIL_IR_EXPR_LOCAL_REF, .ty = T_U32, .index = 1u};
    function->expr_count = 9u;
    function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_LOCAL_DECL, .local = 0u,
        .is_const = true, .has_initializer = true, .initializer = 3u};
    function->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 1u, .assign_rhs = 7u};
    function->stmts[2] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 5u, .assign_rhs = 6u};
    function->stmts[3] = (DxilIrStmt){.kind = DXIL_IR_STMT_LOCAL_DECL, .local = 1u,
        .is_const = true, .has_initializer = true, .initializer = 7u};
    function->stmts[4] = (DxilIrStmt){.kind = DXIL_IR_STMT_EXPR, .expr = 8u};
    function->stmts[5] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 5u}};
    function->stmt_count = 6u; function->root_stmt = 5u;
    function->stmt_child_count = 5u;
    for (uint32_t i = 0u; i < 5u; ++i) function->stmt_children[i] = i;
    if (emit()) {
        const char *capture = strstr(output, "const uint doe_pointer_index_2 = index;");
        const char *mutate = strstr(output, "index = 2;");
        const char *write = strstr(output, "buf[doe_pointer_index_2] = 9;");
        CHECK(capture != NULL && mutate != NULL && write != NULL);
        CHECK(capture != NULL && mutate != NULL && write != NULL && capture < mutate && mutate < write);
        CHECK(strstr(output, "const uint __doe_local_1 = 2;") != NULL);
        CHECK(strstr(output, "    __doe_local_1;") != NULL);
    }
    function->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 1u};
    function->stmt_children[1] = 2u;
    if (emit()) CHECK(strstr(output, "doe_pointer_index_") == NULL);
}
static void temporary_name_collisions(void) {
    runtime_write();
    DxilIrFunction *function = &module.functions[0];
    function->params[0].name = "doe_store_rhs_0";
    function->exprs[7] = (DxilIrExpr){.kind = DXIL_IR_EXPR_PARAM_REF, .ty = T_U32, .index = 0u};
    if (emit()) {
        CHECK(strstr(output, "void main(uint doe_store_rhs_0 : SV_GroupIndex)") != NULL);
        CHECK(strstr(output, "const uint __doe_temporary_0_doe_store_rhs_0 = doe_store_rhs_0;") != NULL);
        CHECK(strstr(output, "const uint doe_store_rhs_0 = doe_store_rhs_0;") == NULL);
        CHECK(strstr(output, "buf.tail[__doe_temporary_0_doe_store_expr_0_5] = __doe_temporary_0_doe_store_rhs_0;") != NULL);
    }
    function->params[0].name = "index";
    module.global_count = 2u;
    module.globals[1] = (DxilIrGlobal){.name = "doe_store_rhs_0", .ty = T_U32, .class = DXIL_IR_GLOBAL_VAR};
    function->exprs[7] = (DxilIrExpr){.kind = DXIL_IR_EXPR_GLOBAL_REF, .ty = T_U32, .index = 1u};
    if (emit()) CHECK(strstr(output, "const uint __doe_temporary_0_doe_store_rhs_0 = doe_store_rhs_0;") != NULL);
    module.globals[1].name = "ordinary_global";
    function->local_count = 1u;
    function->locals[0] = (DxilIrLocal){.name = "doe_store_rhs_0", .ty = T_U32};
    function->exprs[7] = (DxilIrExpr){.kind = DXIL_IR_EXPR_LOCAL_REF, .ty = T_U32, .index = 0u};
    function->exprs[9] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 21u};
    function->expr_count = 10u;
    function->stmts[2] = (DxilIrStmt){.kind = DXIL_IR_STMT_LOCAL_DECL, .local = 0u,
        .is_const = true, .has_initializer = true, .initializer = 9u};
    function->stmt_count = 3u;
    function->stmts[1].range.len = 2u;
    function->stmt_children[0] = 2u; function->stmt_children[1] = 0u;
    function->stmt_child_count = 2u;
    if (emit()) {
        CHECK(strstr(output, "const uint doe_store_rhs_0 = 21;") != NULL);
        CHECK(strstr(output, "const uint __doe_temporary_0_doe_store_rhs_0 = doe_store_rhs_0;") != NULL);
    }
    module.globals[1].name = "__doe_temporary_0_doe_store_rhs_0";
    if (emit()) CHECK(strstr(output, "const uint __doe_temporary_1_doe_store_rhs_0 = doe_store_rhs_0;") != NULL);
    module.globals[1].name = "__doe_temporary_4294967295_doe_store_rhs_0";
    if (emit()) CHECK(strstr(output, "const uint __doe_temporary_0_doe_store_rhs_0 = doe_store_rhs_0;") != NULL);
    function->locals[0].name = "ordinary_local";
    if (emit()) {
        CHECK(strstr(output, "const uint doe_store_rhs_0 = ordinary_local;") != NULL);
        CHECK(strstr(output, "__doe_temporary_0_doe_store_expr_") == NULL);
    }

    function = base(false);
    function->params[0].name = "doe_pointer_index_2";
    function->exprs[2] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INDEX, .ty = T_U32,
        .category = DXIL_IR_EXPR_REF, .base = 0u, .index_expr = 1u};
    function->exprs[3] = (DxilIrExpr){.kind = DXIL_IR_EXPR_ADDRESS_OF, .ty = T_POINTER, .inner = 2u};
    function->exprs[4] = (DxilIrExpr){.kind = DXIL_IR_EXPR_LOCAL_REF, .ty = T_POINTER, .index = 0u};
    function->exprs[5] = (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT, .ty = T_U32, .uint_value = 9u};
    function->expr_count = 6u;
    function->locals[0] = (DxilIrLocal){.name = "p", .ty = T_POINTER};
    function->local_count = 1u;
    function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_LOCAL_DECL, .local = 0u,
        .is_const = true, .has_initializer = true, .initializer = 3u};
    function->stmts[1] = (DxilIrStmt){.kind = DXIL_IR_STMT_ASSIGN, .assign_lhs = 4u, .assign_rhs = 5u};
    function->stmts[2] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK, .range = {0u, 2u}};
    function->stmt_count = 3u; function->root_stmt = 2u;
    function->stmt_children[0] = 0u; function->stmt_children[1] = 1u;
    function->stmt_child_count = 2u;
    if (emit()) {
        CHECK(strstr(output, "const uint __doe_temporary_0_doe_pointer_index_2 = doe_pointer_index_2;") != NULL);
        CHECK(strstr(output, "buf[__doe_temporary_0_doe_pointer_index_2] = 9;") != NULL);
    }
}
static void invalid_keeps_output(void) {
    DxilIrFunction *function = base(false);
    function->stmts[0] = (DxilIrStmt){.kind = DXIL_IR_STMT_BLOCK};
    function->stmt_count = 1u;
    module.type_count = DXIL_IR_MAX_TYPES + 1u;
    memset(output, 'x', sizeof(output)); written = 777u;
    CHECK_STATUS(dxil_hlsl_emit(&module, output, sizeof(output), &written), DXIL_INVALID_IR);
    CHECK(output[0] == 'x' && written == 777u);
    module.type_count = 4u; module.types[T_POINTER].elem = T_POINTER;
    CHECK_STATUS(dxil_hlsl_emit(&module, output, sizeof(output), &written), DXIL_INVALID_IR);
    CHECK(output[0] == 'x' && written == 777u);
    module.types[T_POINTER].elem = T_U32; module.globals[0].name = NULL;
    CHECK_STATUS(dxil_hlsl_emit(&module, output, sizeof(output), &written), DXIL_INVALID_IR);
    CHECK(output[0] == 'x' && written == 777u);
    module.globals[0].name = "buf";
    function->exprs[1] = (DxilIrExpr){.kind = DXIL_IR_EXPR_CALL, .ty = T_U32, .name = "min",
        .call_kind = DXIL_IR_CALL_BUILTIN, .args = {UINT32_MAX, 2u}};
    CHECK_STATUS(dxil_hlsl_emit(&module, output, sizeof(output), &written), DXIL_INVALID_IR);
    CHECK(output[0] == 'x' && written == 777u);
}
int main(void) {
    runtime_write();
    runtime_pointer_snapshot();
    store_effects();
    pointer_snapshot();
    temporary_name_collisions();
    invalid_keeps_output();
    TEST_SUMMARY("test_dxil_hlsl_access");
}
