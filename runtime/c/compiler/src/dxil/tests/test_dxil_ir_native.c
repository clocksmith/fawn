/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"

#include "test_bitcode_scan.h"

static bool contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

#include <stdio.h>

#include "dxil_diagnostic.h"
#include "dxil_ir.h"
#include "dxil_native.h"
#include "dxil_spec.h"
#include "dxil_validate.h"

static DxilIrTypeId scalar_ty(DxilIrModule *module,
    DxilIrScalar scalar) {
    DxilIrType type;
    DxilIrTypeId id = DXIL_IR_INVALID_ID;
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_IR_TYPE_SCALAR;
    type.scalar = scalar;
    if (dxil_ir_intern_type(module, &type, &id) != DXIL_OK) {
        return DXIL_IR_INVALID_ID;
    }
    return id;
}

static DxilIrTypeId vec_ty(DxilIrModule *module, DxilIrTypeId elem,
    uint8_t len) {
    DxilIrType type;
    DxilIrTypeId id = DXIL_IR_INVALID_ID;
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_IR_TYPE_VECTOR;
    type.elem = elem;
    type.len = len;
    if (dxil_ir_intern_type(module, &type, &id) != DXIL_OK) {
        return DXIL_IR_INVALID_ID;
    }
    return id;
}

/* Canonical Zig native test module: compute main, workgroup 64,1,1,
 * one vec3<u32> parameter with global_invocation_id, empty root. */
static void build_canonical(DxilIrModule *module) {
    DxilIrFunction *function = NULL;
    DxilIrStmt block;
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrEntryPoint entry;
    DxilIrTypeId void_ty = scalar_ty(module, DXIL_IR_SCALAR_VOID);
    DxilIrTypeId u32_ty = scalar_ty(module, DXIL_IR_SCALAR_U32);
    DxilIrTypeId vec3_ty = vec_ty(module, u32_ty, 3);
    dxil_ir_add_function(module, "main", void_ty, &function);
    function->params[0].name = "gid";
    function->params[0].ty = vec3_ty;
    function->params[0].has_io = true;
    function->params[0].io.builtin =
        DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID;
    function->param_count = 1;
    memset(&block, 0, sizeof(block));
    block.kind = DXIL_IR_STMT_BLOCK;
    dxil_ir_append_stmt(function, &block, &block_id);
    function->root_stmt = block_id;
    memset(&entry, 0, sizeof(entry));
    entry.function = 0;
    entry.stage = DXIL_IR_STAGE_COMPUTE;
    entry.workgroup_size[0] = 64;
    entry.workgroup_size[1] = 1;
    entry.workgroup_size[2] = 1;
    dxil_ir_add_entry_point(module, &entry);
}

static void check_interning(void) {
    DxilIrModule module;
    DxilIrType type;
    DxilIrTypeId i32_a = DXIL_IR_INVALID_ID;
    DxilIrTypeId i32_b = DXIL_IR_INVALID_ID;
    DxilIrTypeId vec_a = DXIL_IR_INVALID_ID;
    DxilIrTypeId vec_b = DXIL_IR_INVALID_ID;
    DxilIrTypeId struct_a = DXIL_IR_INVALID_ID;
    DxilIrTypeId struct_b = DXIL_IR_INVALID_ID;
    DxilIrStructId struct_id = DXIL_IR_INVALID_ID;
    dxil_ir_init(&module);
    i32_a = scalar_ty(&module, DXIL_IR_SCALAR_I32);
    i32_b = scalar_ty(&module, DXIL_IR_SCALAR_I32);
    CHECK(i32_a != DXIL_IR_INVALID_ID && i32_a == i32_b);
    vec_a = vec_ty(&module, scalar_ty(&module, DXIL_IR_SCALAR_F32),
        2);
    vec_b = vec_ty(&module, scalar_ty(&module, DXIL_IR_SCALAR_F32),
        2);
    CHECK(vec_a != DXIL_IR_INVALID_ID && vec_a == vec_b);
    CHECK(dxil_ir_is_float_type(&module, vec_a));
    CHECK(!dxil_ir_is_float_type(&module, i32_a));
    CHECK(dxil_ir_is_signed_type(&module, i32_a));
    CHECK(!dxil_ir_is_signed_type(&module,
        scalar_ty(&module, DXIL_IR_SCALAR_U32)));
    dxil_ir_add_struct(&module, "S", &struct_id);
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_IR_TYPE_STRUCT;
    type.struct_id = struct_id;
    CHECK(dxil_ir_intern_type(&module, &type, &struct_a) == DXIL_OK);
    CHECK(dxil_ir_intern_type(&module, &type, &struct_b) == DXIL_OK);
    CHECK(struct_a != DXIL_IR_INVALID_ID && struct_a == struct_b);
    {
        DxilIrStructId other_id = DXIL_IR_INVALID_ID;
        DxilIrTypeId other_ty = DXIL_IR_INVALID_ID;
        dxil_ir_add_struct(&module, "T", &other_id);
        memset(&type, 0, sizeof(type));
        type.kind = DXIL_IR_TYPE_STRUCT;
        type.struct_id = other_id;
        CHECK(dxil_ir_intern_type(&module, &type, &other_ty)
            == DXIL_OK);
        CHECK(other_ty != struct_a);
    }
}

static void check_layout(void) {
    DxilIrModule module;
    DxilIrStructId struct_id = DXIL_IR_INVALID_ID;
    DxilIrStructField field;
    DxilIrType type;
    DxilIrTypeId struct_ty = DXIL_IR_INVALID_ID;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t binding = 0;
    dxil_ir_init(&module);
    dxil_ir_add_struct(&module, "S", &struct_id);
    memset(&field, 0, sizeof(field));
    field.name = "a";
    field.ty = scalar_ty(&module, DXIL_IR_SCALAR_U32);
    CHECK(dxil_ir_add_struct_field(&module, struct_id, &field)
        == DXIL_OK);
    memset(&field, 0, sizeof(field));
    field.name = "b";
    field.ty = vec_ty(&module,
        scalar_ty(&module, DXIL_IR_SCALAR_F32), 3);
    CHECK(dxil_ir_add_struct_field(&module, struct_id, &field)
        == DXIL_OK);
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_IR_TYPE_STRUCT;
    type.struct_id = struct_id;
    CHECK(dxil_ir_intern_type(&module, &type, &struct_ty)
        == DXIL_OK);
    CHECK(dxil_ir_type_alignment(&module, struct_ty) == 16);
    CHECK(dxil_ir_type_size(&module, struct_ty) == 32);
    CHECK(dxil_ir_struct_field_offset(&module, struct_id, 0,
        &offset) == DXIL_OK && offset == 0);
    CHECK(dxil_ir_struct_field_offset(&module, struct_id, 1,
        &offset) == DXIL_OK && offset == 16);
    CHECK(dxil_ir_minimum_binding_size(&module, struct_ty,
        &binding) == DXIL_OK && binding == 32);
    size = dxil_ir_type_size(&module, vec_ty(&module,
        scalar_ty(&module, DXIL_IR_SCALAR_F32), 3));
    CHECK(size == 12);
}

static void check_queries(void) {
    DxilIrModule module;
    DxilIrFunction *function = NULL;
    DxilIrExpr expr;
    DxilIrExprId lit = DXIL_IR_INVALID_ID;
    DxilIrExprId ref = DXIL_IR_INVALID_ID;
    DxilIrExprId member = DXIL_IR_INVALID_ID;
    uint64_t value = 0;
    uint8_t component = 0;
    DxilIrTypeId void_ty = DXIL_IR_INVALID_ID;
    DxilIrTypeId u32_ty = DXIL_IR_INVALID_ID;
    DxilIrTypeId vec3_ty = DXIL_IR_INVALID_ID;
    dxil_ir_init(&module);
    void_ty = scalar_ty(&module, DXIL_IR_SCALAR_VOID);
    u32_ty = scalar_ty(&module, DXIL_IR_SCALAR_U32);
    vec3_ty = vec_ty(&module, u32_ty, 3);
    dxil_ir_add_function(&module, "main", void_ty, &function);
    function->params[0].name = "gid";
    function->params[0].ty = vec3_ty;
    function->params[0].has_io = true;
    function->params[0].io.builtin =
        DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID;
    function->param_count = 1;
    memset(&expr, 0, sizeof(expr));
    expr.kind = DXIL_IR_EXPR_INT_LIT;
    expr.ty = u32_ty;
    expr.uint_value = 7;
    dxil_ir_append_expr(function, &expr, &lit);
    CHECK(dxil_ir_match_int_literal(function, lit, &value)
        && value == 7);
    CHECK(dxil_ir_matches_int_literal(function, lit, 7));
    CHECK(!dxil_ir_matches_int_literal(function, lit, 8));
    memset(&expr, 0, sizeof(expr));
    expr.kind = DXIL_IR_EXPR_PARAM_REF;
    expr.ty = vec3_ty;
    expr.index = 0;
    dxil_ir_append_expr(function, &expr, &ref);
    memset(&expr, 0, sizeof(expr));
    expr.kind = DXIL_IR_EXPR_MEMBER;
    expr.ty = u32_ty;
    expr.base = ref;
    expr.field_name = "y";
    expr.field_index = 1;
    dxil_ir_append_expr(function, &expr, &member);
    CHECK(dxil_ir_classify_builtin_component(function, member,
        DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID, &component)
        && component == 1);
    CHECK(!dxil_ir_classify_builtin_component(function, member,
        DXIL_IR_BUILTIN_WORKGROUP_ID, &component));
    CHECK(!dxil_ir_type_has_io_struct_field(&module, vec3_ty));
}

static void check_native_canonical(void) {
    DxilIrModule module;
    static uint8_t out[65536];
    size_t written = 0;
    DxilValidationResult result;
    const uint8_t *bitcode = NULL;
    size_t bitcode_len = 0;
    DxilScanResult scan;
    dxil_ir_init(&module);
    build_canonical(&module);
    CHECK_STATUS(dxil_native_emit(&module, out, sizeof(out),
        &written), DXIL_OK);
    CHECK(written > 32);
    CHECK(memcmp(out, "DXBC", 4) == 0);
    dxil_validate(out, written, &result);
    CHECK(result.valid);
    CHECK(result.has_shader_kind && result.shader_kind == 5);
    CHECK(result.part_count == 4);
    CHECK(container_bitcode(out, written, &bitcode, &bitcode_len));
    CHECK(scan_bitcode(bitcode, bitcode_len, &scan));
    CHECK(scan.ok);
    CHECK(scan.function_blocks == 1);
    CHECK(scan.call_records == 3);
    CHECK(scan.function_saw_ret_last);
    CHECK(contains(scan.metadata_text, "dx.version"));
    CHECK(contains(scan.metadata_text, "dx.valver"));
    CHECK(contains(scan.metadata_text, "dx.shaderModel"));
    CHECK(contains(scan.metadata_text, "dx.entryPoints"));
    CHECK(contains(scan.metadata_text, "cs"));
}

static void check_native_barrier(void) {
    DxilIrModule module;
    DxilIrFunction *function = NULL;
    DxilIrTypeId void_ty = DXIL_IR_INVALID_ID;
    DxilIrStmt block;
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrStmt expr_stmt;
    DxilIrExpr call;
    DxilIrExprId call_id = DXIL_IR_INVALID_ID;
    DxilIrEntryPoint entry;
    static uint8_t out[65536];
    size_t written = 0;
    const uint8_t *bitcode = NULL;
    size_t bitcode_len = 0;
    DxilScanResult scan;
    dxil_ir_init(&module);
    void_ty = scalar_ty(&module, DXIL_IR_SCALAR_VOID);
    dxil_ir_add_function(&module, "main", void_ty, &function);
    memset(&call, 0, sizeof(call));
    call.kind = DXIL_IR_EXPR_CALL;
    call.ty = void_ty;
    call.name = "workgroupBarrier";
    call.call_kind = DXIL_IR_CALL_BUILTIN;
    dxil_ir_append_expr(function, &call, &call_id);
    memset(&expr_stmt, 0, sizeof(expr_stmt));
    expr_stmt.kind = DXIL_IR_STMT_EXPR;
    expr_stmt.expr = call_id;
    {
        DxilIrStmtId stmt_id = DXIL_IR_INVALID_ID;
        dxil_ir_append_stmt(function, &expr_stmt, &stmt_id);
        memset(&block, 0, sizeof(block));
        block.kind = DXIL_IR_STMT_BLOCK;
        block.range.start = 0;
        block.range.len = 1;
        dxil_ir_append_stmt_child(function, stmt_id);
        dxil_ir_append_stmt(function, &block, &block_id);
    }
    function->root_stmt = block_id;
    memset(&entry, 0, sizeof(entry));
    entry.function = 0;
    entry.stage = DXIL_IR_STAGE_COMPUTE;
    entry.workgroup_size[0] = 1;
    entry.workgroup_size[1] = 1;
    entry.workgroup_size[2] = 1;
    dxil_ir_add_entry_point(&module, &entry);
    CHECK_STATUS(dxil_native_emit(&module, out, sizeof(out),
        &written), DXIL_OK);
    CHECK(container_bitcode(out, written, &bitcode, &bitcode_len));
    CHECK(scan_bitcode(bitcode, bitcode_len, &scan));
    CHECK(scan.ok);
    CHECK(scan.function_blocks == 1);
    CHECK(scan.call_records == 1);
    CHECK(scan.function_saw_ret_last);
}

static void check_native_errors(void) {
    DxilIrModule module;
    DxilIrFunction *function = NULL;
    DxilIrTypeId void_ty = DXIL_IR_INVALID_ID;
    DxilIrTypeId u32_ty = DXIL_IR_INVALID_ID;
    static uint8_t out[65536];
    size_t written = 0;
    DxilIrStmt block;
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrEntryPoint entry;
    dxil_ir_init(&module);
    void_ty = scalar_ty(&module, DXIL_IR_SCALAR_VOID);
    u32_ty = scalar_ty(&module, DXIL_IR_SCALAR_U32);
    (void)u32_ty;
    dxil_ir_add_function(&module, "main", void_ty, &function);
    memset(&block, 0, sizeof(block));
    block.kind = DXIL_IR_STMT_BLOCK;
    dxil_ir_append_stmt(function, &block, &block_id);
    function->root_stmt = block_id;
    CHECK_STATUS(dxil_native_emit(&module, out, sizeof(out),
        &written), DXIL_INVALID_IR);
    memset(&entry, 0, sizeof(entry));
    entry.function = 9;
    entry.stage = DXIL_IR_STAGE_COMPUTE;
    dxil_ir_add_entry_point(&module, &entry);
    CHECK_STATUS(dxil_native_emit(&module, out, sizeof(out),
        &written), DXIL_INVALID_IR);
}

static void check_native_placeholders(void) {
    DxilIrModule module;
    DxilIrFunction *function = NULL;
    DxilIrTypeId void_ty = DXIL_IR_INVALID_ID;
    DxilIrTypeId u32_ty = DXIL_IR_INVALID_ID;
    DxilIrStmt block;
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrStmt assign;
    DxilIrStmtId assign_id = DXIL_IR_INVALID_ID;
    DxilIrExpr addr;
    DxilIrExpr param_ref;
    DxilIrExprId addr_id = DXIL_IR_INVALID_ID;
    DxilIrExprId ref_id = DXIL_IR_INVALID_ID;
    DxilIrEntryPoint entry;
    static uint8_t out[65536];
    size_t written = 0;
    dxil_ir_init(&module);
    void_ty = scalar_ty(&module, DXIL_IR_SCALAR_VOID);
    u32_ty = scalar_ty(&module, DXIL_IR_SCALAR_U32);
    dxil_ir_add_function(&module, "main", void_ty, &function);
    function->params[0].name = "x";
    function->params[0].ty = u32_ty;
    function->param_count = 1;
    memset(&param_ref, 0, sizeof(param_ref));
    param_ref.kind = DXIL_IR_EXPR_PARAM_REF;
    param_ref.ty = u32_ty;
    param_ref.index = 0;
    dxil_ir_append_expr(function, &param_ref, &ref_id);
    memset(&addr, 0, sizeof(addr));
    addr.kind = DXIL_IR_EXPR_ADDRESS_OF;
    addr.ty = u32_ty;
    addr.inner = ref_id;
    dxil_ir_append_expr(function, &addr, &addr_id);
    memset(&assign, 0, sizeof(assign));
    assign.kind = DXIL_IR_STMT_ASSIGN;
    assign.assign_op = DXIL_IR_ASSIGN_ASSIGN;
    assign.assign_lhs = addr_id;
    assign.assign_rhs = ref_id;
    dxil_ir_append_stmt(function, &assign, &assign_id);
    memset(&block, 0, sizeof(block));
    block.kind = DXIL_IR_STMT_BLOCK;
    block.range.start = 0;
    block.range.len = 1;
    dxil_ir_append_stmt_child(function, assign_id);
    dxil_ir_append_stmt(function, &block, &block_id);
    function->root_stmt = block_id;
    memset(&entry, 0, sizeof(entry));
    entry.function = 0;
    entry.stage = DXIL_IR_STAGE_COMPUTE;
    dxil_ir_add_entry_point(&module, &entry);
    CHECK_STATUS(dxil_native_emit(&module, out, sizeof(out),
        &written), DXIL_OK);
    memset(&assign, 0, sizeof(assign));
    assign.kind = DXIL_IR_STMT_ASSIGN;
    assign.assign_op = DXIL_IR_ASSIGN_ASSIGN;
    assign.assign_lhs = ref_id;
    assign.assign_rhs = addr_id;
    {
        DxilIrModule module2;
        DxilIrFunction *fn2 = NULL;
        DxilIrStmt block2;
        DxilIrStmtId block2_id = DXIL_IR_INVALID_ID;
        DxilIrStmtId assign2_id = DXIL_IR_INVALID_ID;
        DxilIrExprId ref2 = DXIL_IR_INVALID_ID;
        DxilIrExprId addr2 = DXIL_IR_INVALID_ID;
        DxilIrEntryPoint entry2;
        dxil_ir_init(&module2);
        void_ty = scalar_ty(&module2, DXIL_IR_SCALAR_VOID);
        u32_ty = scalar_ty(&module2, DXIL_IR_SCALAR_U32);
        dxil_ir_add_function(&module2, "main", void_ty, &fn2);
        fn2->params[0].name = "x";
        fn2->params[0].ty = u32_ty;
        fn2->param_count = 1;
        memset(&param_ref, 0, sizeof(param_ref));
        param_ref.kind = DXIL_IR_EXPR_PARAM_REF;
        param_ref.ty = u32_ty;
        param_ref.index = 0;
        dxil_ir_append_expr(fn2, &param_ref, &ref2);
        memset(&addr, 0, sizeof(addr));
        addr.kind = DXIL_IR_EXPR_ADDRESS_OF;
        addr.ty = u32_ty;
        addr.inner = ref2;
        dxil_ir_append_expr(fn2, &addr, &addr2);
        memset(&assign, 0, sizeof(assign));
        assign.kind = DXIL_IR_STMT_ASSIGN;
        assign.assign_op = DXIL_IR_ASSIGN_ASSIGN;
        assign.assign_lhs = ref2;
        assign.assign_rhs = addr2;
        dxil_ir_append_stmt(fn2, &assign, &assign2_id);
        memset(&block2, 0, sizeof(block2));
        block2.kind = DXIL_IR_STMT_BLOCK;
        block2.range.start = 0;
        block2.range.len = 1;
        dxil_ir_append_stmt_child(fn2, assign2_id);
        dxil_ir_append_stmt(fn2, &block2, &block2_id);
        fn2->root_stmt = block2_id;
        memset(&entry2, 0, sizeof(entry2));
        entry2.function = 0;
        entry2.stage = DXIL_IR_STAGE_COMPUTE;
        dxil_ir_add_entry_point(&module2, &entry2);
        CHECK_STATUS(dxil_native_emit(&module2, out, sizeof(out),
            &written), DXIL_UNSUPPORTED_CONSTRUCT);
    }
}

static void check_native_resources(void) {
    DxilIrModule module;
    DxilIrFunction *function = NULL;
    DxilIrTypeId void_ty = DXIL_IR_INVALID_ID;
    DxilIrTypeId u32_ty = DXIL_IR_INVALID_ID;
    DxilIrStmt block;
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrEntryPoint entry;
    static uint8_t out[65536];
    size_t written = 0;
    uint32_t i = 0;
    dxil_ir_init(&module);
    void_ty = scalar_ty(&module, DXIL_IR_SCALAR_VOID);
    u32_ty = scalar_ty(&module, DXIL_IR_SCALAR_U32);
    dxil_ir_add_function(&module, "main", void_ty, &function);
    memset(&block, 0, sizeof(block));
    block.kind = DXIL_IR_STMT_BLOCK;
    dxil_ir_append_stmt(function, &block, &block_id);
    function->root_stmt = block_id;
    memset(&entry, 0, sizeof(entry));
    entry.function = 0;
    entry.stage = DXIL_IR_STAGE_COMPUTE;
    dxil_ir_add_entry_point(&module, &entry);
    for (i = 0; i < 64; i++) {
        DxilIrGlobal global;
        DxilIrGlobalId id = DXIL_IR_INVALID_ID;
        memset(&global, 0, sizeof(global));
        global.name = "buf";
        global.ty = u32_ty;
        global.class = DXIL_IR_GLOBAL_VAR;
        global.has_addr_space = true;
        global.addr_space = DXIL_IR_ADDR_UNIFORM;
        global.has_binding = true;
        global.binding.group = 0;
        global.binding.binding = i;
        CHECK(dxil_ir_add_global(&module, &global, &id) == DXIL_OK);
    }
    {
        DxilIrGlobal global;
        DxilIrGlobalId id = DXIL_IR_INVALID_ID;
        memset(&global, 0, sizeof(global));
        global.name = "unbound";
        global.ty = u32_ty;
        global.class = DXIL_IR_GLOBAL_VAR;
        global.has_addr_space = true;
        global.addr_space = DXIL_IR_ADDR_UNIFORM;
        CHECK(dxil_ir_add_global(&module, &global, &id) == DXIL_OK);
    }
    {
        DxilIrGlobal global;
        DxilIrGlobalId id = DXIL_IR_INVALID_ID;
        memset(&global, 0, sizeof(global));
        global.name = "unclassifiable";
        global.ty = u32_ty;
        global.class = DXIL_IR_GLOBAL_VAR;
        global.has_addr_space = true;
        global.addr_space = DXIL_IR_ADDR_FUNCTION;
        global.has_binding = true;
        global.binding.group = 0;
        global.binding.binding = 99;
        CHECK(dxil_ir_add_global(&module, &global, &id) == DXIL_OK);
    }
    CHECK_STATUS(dxil_native_emit(&module, out, sizeof(out),
        &written), DXIL_OK);
    {
        DxilIrGlobal global;
        DxilIrGlobalId id = DXIL_IR_INVALID_ID;
        memset(&global, 0, sizeof(global));
        global.name = "overflow";
        global.ty = u32_ty;
        global.class = DXIL_IR_GLOBAL_VAR;
        global.has_addr_space = true;
        global.addr_space = DXIL_IR_ADDR_UNIFORM;
        global.has_binding = true;
        global.binding.group = 0;
        global.binding.binding = 64;
        CHECK(dxil_ir_add_global(&module, &global, &id) == DXIL_OK);
    }
    CHECK_STATUS(dxil_native_emit(&module, out, sizeof(out),
        &written), DXIL_OUTPUT_TOO_LARGE);
}

int main(void) {
    check_interning();
    check_layout();
    check_queries();
    check_native_canonical();
    check_native_barrier();
    check_native_errors();
    check_native_placeholders();
    check_native_resources();
    TEST_SUMMARY("test_dxil_ir_native");
}
