/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"

#include "tsir_bootstrap.h"
#include "tsir_emit.h"
#include "tsir_extended.h"
#include "tsir_frontend.h"
#include "tsir_reference.h"
#include "tsir_sha256.h"
#include "tsir_transcendental.h"
#include "tsir_wgsl_ir.h"

/* New pieces: WGSL IR frontend (T-FIR), extended ops (T-EXT),
 * transcendentals (T-TRANS), kernel bodies (T-KB), bootstrap
 * tools (T-BOOT). Fixtures are built in C; no Zig toolchain, no
 * GPU, no network, no fawn-tree at run time. Transcendental
 * values are asserted exactly only where exactly representable;
 * everything else uses a stated tolerance, because bit parity
 * against Zig is unestablished (see gaps.md). */

static void put_f32s(uint8_t *buf, const float *vals, size_t count) {
    size_t i = 0;
    for (i = 0; i < count; i++) {
        put_f32(buf, i, vals[i]);
    }
}

static bool near_f32(float got, float want, float tol) {
    float diff = got - want;
    if (diff < 0) {
        diff = -diff;
    }
    return diff <= tol;
}

static void check_f32_near(const uint8_t *buf, size_t index,
    float want, float tol) {
    CHECK(near_f32(get_f32(buf, index), want, tol));
}

/* ---- frontend fixtures ---- */

static TsirIrType fir_types[6];
static TsirIrGlobal fir_globals[3];
static TsirIrExpr fir_exprs[8];
static TsirIrStmt fir_stmts[6];
static uint32_t fir_children[4];
static TsirIrLocal fir_locals[1];
static TsirIrFunction fir_func;
static TsirWgslModule fir_module;

static void fir_init_types(void) {
    memset(fir_types, 0, sizeof(fir_types));
    fir_types[0].kind = TSIR_IR_TYPE_SCALAR;
    fir_types[0].scalar = TSIR_IR_F32;
    fir_types[1].kind = TSIR_IR_TYPE_SCALAR;
    fir_types[1].scalar = TSIR_IR_U32;
    fir_types[2].kind = TSIR_IR_TYPE_VECTOR;
    fir_types[2].elem = 1;
    fir_types[2].len = 3;
    fir_types[3].kind = TSIR_IR_TYPE_ARRAY;
    fir_types[3].elem = 0;
    fir_types[3].has_len = true;
    fir_types[3].array_len = 3;
    fir_types[4].kind = TSIR_IR_TYPE_ARRAY;
    fir_types[4].elem = 3;
    fir_types[4].has_len = true;
    fir_types[4].array_len = 2;
    fir_types[5].kind = TSIR_IR_TYPE_ARRAY;
    fir_types[5].elem = 0;
    fir_types[5].has_len = true;
    fir_types[5].array_len = 2;
    memset(fir_globals, 0, sizeof(fir_globals));
    fir_globals[0].name = "W";
    fir_globals[0].ty = 4;
    fir_globals[0].class = TSIR_IR_GLOBAL_VAR;
    fir_globals[0].has_addr_space = true;
    fir_globals[0].addr_space = TSIR_IR_SPACE_STORAGE;
    fir_globals[0].has_access = true;
    fir_globals[0].access = TSIR_IR_ACCESS_READ;
    fir_globals[0].has_binding = true;
    fir_globals[0].group = 0;
    fir_globals[0].binding = 0;
    fir_globals[1].name = "out";
    fir_globals[1].ty = 5;
    fir_globals[1].class = TSIR_IR_GLOBAL_VAR;
    fir_globals[1].has_addr_space = true;
    fir_globals[1].addr_space = TSIR_IR_SPACE_STORAGE;
    fir_globals[1].has_access = true;
    fir_globals[1].access = TSIR_IR_ACCESS_READ_WRITE;
    fir_globals[1].has_binding = true;
    fir_globals[1].group = 0;
    fir_globals[1].binding = 1;
    fir_globals[2].name = "unused";
    fir_globals[2].ty = 4;
    fir_globals[2].class = TSIR_IR_GLOBAL_VAR;
    fir_globals[2].has_addr_space = true;
    fir_globals[2].addr_space = TSIR_IR_SPACE_STORAGE;
    fir_globals[2].has_access = true;
    fir_globals[2].access = TSIR_IR_ACCESS_READ;
    fir_globals[2].has_binding = true;
    fir_globals[2].group = 0;
    fir_globals[2].binding = 2;
    fir_module.types = fir_types;
    fir_module.type_count = 6;
    fir_module.globals = fir_globals;
    fir_module.global_count = 3;
    fir_module.functions = &fir_func;
    fir_module.function_count = 1;
}

static void test_frontend_layout_and_query(void) {
    TsirIrStructField fields[2];
    TsirIrStructDef def;
    uint32_t size = 0;
    uint32_t offset = 0;
    uint32_t base = 0;
    uint64_t lit = 0;
    TsirIrType types[4];
    TsirIrFunction func;
    TsirIrExpr exprs[3];
    memset(types, 0, sizeof(types));
    types[0].kind = TSIR_IR_TYPE_SCALAR;
    types[0].scalar = TSIR_IR_F32;
    types[1].kind = TSIR_IR_TYPE_VECTOR;
    types[1].elem = 0;
    types[1].len = 3;
    types[2].kind = TSIR_IR_TYPE_STRUCT;
    types[2].struct_id = 0;
    fields[0].name = "a";
    fields[0].ty = 0;
    fields[1].name = "b";
    fields[1].ty = 1;
    def.name = "S";
    def.fields = fields;
    def.field_count = 2;
    {
        TsirWgslModule module;
        memset(&module, 0, sizeof(module));
        module.types = types;
        module.type_count = 3;
        module.structs = &def;
        module.struct_count = 1;
        CHECK(tsir_ir_type_size(&module, 2, &size));
        CHECK(size == 32);
        CHECK(tsir_ir_struct_field_offset(&module, &def, 1,
            &offset));
        CHECK(offset == 16);
    }
    memset(exprs, 0, sizeof(exprs));
    exprs[0].kind = TSIR_IR_EXPR_GLOBAL_REF;
    exprs[0].ref_index = 0;
    exprs[1].kind = TSIR_IR_EXPR_INDEX;
    exprs[1].index_base = 0;
    exprs[1].index_index = 2;
    exprs[2].kind = TSIR_IR_EXPR_INT_LIT;
    exprs[2].int_value = 7;
    memset(&func, 0, sizeof(func));
    func.exprs = exprs;
    func.expr_count = 3;
    CHECK(tsir_ir_find_global_base(&func, 1, &base));
    CHECK(base == 0);
    CHECK(tsir_ir_match_int_literal(&func, 2, &lit));
    CHECK(lit == 7);
}

static void test_frontend_bindings(void) {
    TsirSemantic semantic;
    uint8_t digest[32];
    TsirStatus status = TSIR_OK;
    fir_init_types();
    memset(fir_exprs, 0, sizeof(fir_exprs));
    fir_exprs[0].kind = TSIR_IR_EXPR_GLOBAL_REF;
    fir_exprs[0].ref_index = 0;
    fir_exprs[1].kind = TSIR_IR_EXPR_GLOBAL_REF;
    fir_exprs[1].ref_index = 1;
    memset(fir_stmts, 0, sizeof(fir_stmts));
    fir_stmts[0].kind = TSIR_IR_STMT_BLOCK;
    fir_stmts[0].range_start = 0;
    fir_stmts[0].range_len = 2;
    fir_stmts[1].kind = TSIR_IR_STMT_EXPR;
    fir_stmts[1].expr = 0;
    fir_stmts[2].kind = TSIR_IR_STMT_EXPR;
    fir_stmts[2].expr = 1;
    fir_children[0] = 1;
    fir_children[1] = 2;
    memset(&fir_func, 0, sizeof(fir_func));
    fir_func.name = "main";
    fir_func.exprs = fir_exprs;
    fir_func.expr_count = 2;
    fir_func.stmts = fir_stmts;
    fir_func.stmt_count = 3;
    fir_func.stmt_children = fir_children;
    fir_func.stmt_child_count = 2;
    fir_func.root_stmt = 0;
    memset(digest, 0x11, 32);
    memset(&semantic, 0, sizeof(semantic));
    status = tsir_frontend_lower(&fir_module, digest, "v1",
        &semantic);
    CHECK_STATUS(status, TSIR_OK);
    if (status == TSIR_OK) {
        const TsirSemanticFunction *func = &semantic.functions[0];
        CHECK(semantic.function_count == 1);
        CHECK(func->binding_count == 2);
        CHECK_STR(func->bindings[0].name, "W");
        CHECK(func->bindings[0].logical_shape_len == 2);
        CHECK(func->bindings[0].logical_shape[0] == 2);
        CHECK(func->bindings[0].logical_shape[1] == 3);
        CHECK(!func->bindings[0].read_write);
        CHECK_STR(func->bindings[1].name, "out");
        CHECK(func->bindings[1].read_write);
        CHECK(func->family_hint == TSIR_HINT_UNKNOWN);
        CHECK(semantic.rejection_count == 0);
        tsir_frontend_free_semantic(&semantic);
    }
}

static void test_frontend_for_axis_and_rejection(void) {
    TsirSemantic semantic;
    uint8_t digest[32];
    TsirStatus status = TSIR_OK;
    fir_init_types();
    memset(fir_exprs, 0, sizeof(fir_exprs));
    fir_exprs[0].kind = TSIR_IR_EXPR_INT_LIT;
    fir_exprs[0].int_value = 0;
    fir_exprs[1].kind = TSIR_IR_EXPR_LOCAL_REF;
    fir_exprs[1].ref_index = 0;
    fir_exprs[2].kind = TSIR_IR_EXPR_INT_LIT;
    fir_exprs[2].int_value = 4;
    fir_exprs[3].kind = TSIR_IR_EXPR_BINARY;
    fir_exprs[3].binary_op = TSIR_IR_BIN_LESS;
    fir_exprs[3].binary_lhs = 1;
    fir_exprs[3].binary_rhs = 2;
    fir_exprs[4].kind = TSIR_IR_EXPR_INT_LIT;
    fir_exprs[4].int_value = 1;
    fir_exprs[5].kind = TSIR_IR_EXPR_LOCAL_REF;
    fir_exprs[5].ref_index = 0;
    fir_exprs[6].kind = TSIR_IR_EXPR_LOAD;
    fir_exprs[6].inner = 1;
    fir_exprs[3].binary_lhs = 6;
    memset(fir_stmts, 0, sizeof(fir_stmts));
    fir_stmts[0].kind = TSIR_IR_STMT_BLOCK;
    fir_stmts[0].range_start = 0;
    fir_stmts[0].range_len = 1;
    fir_stmts[1].kind = TSIR_IR_STMT_LOOP;
    fir_stmts[1].loop_kind = TSIR_IR_LOOP_FOR;
    fir_stmts[1].has_loop_init = true;
    fir_stmts[1].loop_init = 2;
    fir_stmts[1].has_loop_cond = true;
    fir_stmts[1].loop_cond = 3;
    fir_stmts[1].has_loop_continuing = true;
    fir_stmts[1].loop_continuing = 3;
    fir_stmts[1].loop_body = 4;
    fir_stmts[2].kind = TSIR_IR_STMT_LOCAL_DECL;
    fir_stmts[2].local = 0;
    fir_stmts[2].has_initializer = true;
    fir_stmts[2].initializer = 0;
    fir_stmts[3].kind = TSIR_IR_STMT_ASSIGN;
    fir_stmts[3].assign_op = TSIR_IR_ASSIGN_ADD;
    fir_stmts[3].assign_lhs = 5;
    fir_stmts[3].assign_rhs = 4;
    fir_stmts[4].kind = TSIR_IR_STMT_BLOCK;
    fir_children[0] = 1;
    fir_locals[0].name = "i";
    fir_locals[0].ty = 1;
    fir_locals[0].mutable = true;
    memset(&fir_func, 0, sizeof(fir_func));
    fir_func.name = "main";
    fir_func.locals = fir_locals;
    fir_func.local_count = 1;
    fir_func.exprs = fir_exprs;
    fir_func.expr_count = 7;
    fir_func.stmts = fir_stmts;
    fir_func.stmt_count = 5;
    fir_func.stmt_children = fir_children;
    fir_func.stmt_child_count = 1;
    fir_func.root_stmt = 0;
    memset(digest, 0x22, 32);
    memset(&semantic, 0, sizeof(semantic));
    status = tsir_frontend_lower(&fir_module, digest, "v1",
        &semantic);
    CHECK_STATUS(status, TSIR_OK);
    if (status == TSIR_OK) {
        const TsirSemanticFunction *func = &semantic.functions[0];
        CHECK(func->axis_count == 1);
        CHECK_STR(func->axes[0].lower_bound, "0");
        CHECK_STR(func->axes[0].upper_bound, "4");
        CHECK_STR(func->axes[0].step, "1");
        CHECK(func->family_hint == TSIR_HINT_ELEMENTWISE);
        tsir_frontend_free_semantic(&semantic);
    }
    /* While loop: dependence rejection, no axis. */
    memset(fir_stmts, 0, sizeof(fir_stmts));
    fir_stmts[0].kind = TSIR_IR_STMT_BLOCK;
    fir_stmts[0].range_start = 0;
    fir_stmts[0].range_len = 1;
    fir_stmts[1].kind = TSIR_IR_STMT_LOOP;
    fir_stmts[1].loop_kind = TSIR_IR_LOOP_WHILE;
    fir_stmts[1].loop_body = 2;
    fir_stmts[2].kind = TSIR_IR_STMT_BLOCK;
    fir_children[0] = 1;
    memset(&semantic, 0, sizeof(semantic));
    status = tsir_frontend_lower(&fir_module, digest, "v1",
        &semantic);
    CHECK_STATUS(status, TSIR_OK);
    if (status == TSIR_OK) {
        CHECK(semantic.rejection_count == 1);
        CHECK(semantic.rejections[0].reason ==
            TSIR_REJECT_DEPENDENCE_UNANALYZABLE);
        CHECK_STR(semantic.rejections[0].node_path,
            "functions[0].body[0]");
        CHECK_STR(semantic.rejections[0].detail, "while loop");
        tsir_frontend_free_semantic(&semantic);
    }
}

/* ---- extended-op fixtures ---- */

static TsirSemanticFunction ext_func;
static TsirBufferBinding ext_bindings[6];
static TsirSemanticBodyBinding ext_roles[6];
static TsirSemanticBodyAxis ext_axis_roles[2];
static TsirSemantic ext_semantic;

static void ext_setup(TsirSemanticBodyOp op,
    const TsirSemanticBindingRole *roles, size_t role_count,
    size_t binding_count, size_t axis_count) {
    size_t i = 0;
    memset(&ext_func, 0, sizeof(ext_func));
    memset(ext_bindings, 0, sizeof(ext_bindings));
    memset(ext_roles, 0, sizeof(ext_roles));
    for (i = 0; i < role_count; i++) {
        ext_roles[i].binding_index = (uint32_t)i;
        ext_roles[i].role = roles[i];
    }
    ext_func.name = "main";
    ext_func.bindings = ext_bindings;
    ext_func.binding_count = binding_count;
    ext_func.axis_count = axis_count;
    ext_func.body.op = op;
    ext_func.body.binding_roles = ext_roles;
    ext_func.body.binding_role_count = role_count;
    if (axis_count == 1) {
        ext_axis_roles[0].axis_index = 0;
        ext_axis_roles[0].role = TSIR_AXIS_HIDDEN;
        ext_func.body.axis_roles = ext_axis_roles;
        ext_func.body.axis_role_count = 1;
    }
    memset(&ext_semantic, 0, sizeof(ext_semantic));
    ext_semantic.functions = &ext_func;
    ext_semantic.function_count = 1;
}

static void ext_binding(size_t index, uint64_t shape0,
    TsirScalarKind elem, bool read_write) {
    static uint64_t shapes[6];
    ext_bindings[index].name = "b";
    ext_bindings[index].logical_shape = &shapes[index];
    ext_bindings[index].logical_shape_len = 1;
    shapes[index] = shape0;
    ext_bindings[index].elem = elem;
    ext_bindings[index].read_write = read_write;
}

static void test_extended_identity_residual(void) {
    uint8_t in[12];
    uint8_t in_a[12];
    uint8_t in_b[12];
    TsirInputBuffer inputs[3];
    TsirReferenceResult result;
    TsirStatus status = TSIR_OK;
    static const float vals[3] = { 1.0f, 2.0f, 3.0f };
    static const float a_vals[3] = { 1.0f, 2.0f, 3.0f };
    static const float b_vals[3] = { 10.0f, 20.0f, 30.0f };
    static const TsirSemanticBindingRole id_roles[2] = {
        TSIR_ROLE_INPUT, TSIR_ROLE_OUTPUT
    };
    (void)id_roles;
    put_f32s(in, vals, 3);
    ext_setup(TSIR_BODY_UNKNOWN, NULL, 0, 2, 0);
    ext_binding(0, 3, TSIR_SCALAR_F32, false);
    ext_binding(1, 3, TSIR_SCALAR_F32, true);
    inputs[0].data = in;
    inputs[0].len = 12;
    memset(&result, 0, sizeof(result));
    status = tsir_reference_run(&ext_semantic, NULL, inputs, 1,
        &result);
    /* NULL realization is invalid input to the runner; identity
     * is exercised through the empty-realization path below. */
    CHECK_STATUS(status, TSIR_ERR_INVALID);
    {
        TsirRealization realization;
        memset(&realization, 0, sizeof(realization));
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 1, &result);
        CHECK_STATUS(status, TSIR_OK);
        if (status == TSIR_OK) {
            CHECK(result.output_count == 1);
            CHECK(result.output_lens[0] == 12);
            CHECK(memcmp(result.outputs[0], in, 12) == 0);
            tsir_reference_free_result(&result);
        }
    }
    put_f32s(in_a, a_vals, 3);
    put_f32s(in_b, b_vals, 3);
    {
        static const TsirSemanticBindingRole roles[3] = {
            TSIR_ROLE_SUMMAND_A, TSIR_ROLE_SUMMAND_B, TSIR_ROLE_OUTPUT
        };
        TsirRealization realization;
        memset(&realization, 0, sizeof(realization));
        ext_setup(TSIR_BODY_RESIDUAL_ADD, roles, 3, 3, 1);
        ext_binding(0, 3, TSIR_SCALAR_F32, false);
        ext_binding(1, 3, TSIR_SCALAR_F32, false);
        ext_binding(2, 3, TSIR_SCALAR_F32, true);
        inputs[0].data = in_a;
        inputs[0].len = 12;
        inputs[1].data = in_b;
        inputs[1].len = 12;
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 2, &result);
        CHECK_STATUS(status, TSIR_OK);
        if (status == TSIR_OK) {
            check_f32_near(result.outputs[0], 0, 11.0f, 0.0f);
            check_f32_near(result.outputs[0], 1, 22.0f, 0.0f);
            check_f32_near(result.outputs[0], 2, 33.0f, 0.0f);
            tsir_reference_free_result(&result);
        }
    }
}

static void test_extended_gated_attention_l2_conv(void) {
    TsirInputBuffer inputs[4];
    TsirReferenceResult result;
    TsirRealization realization;
    TsirStatus status = TSIR_OK;
    uint8_t gate[4];
    uint8_t gate_in[4];
    memset(&realization, 0, sizeof(realization));
    /* Gated silu: silu(1) * 2, tolerance only. */
    {
        static const TsirSemanticBindingRole roles[3] = {
            TSIR_ROLE_GATE, TSIR_ROLE_INPUT, TSIR_ROLE_OUTPUT
        };
        put_f32(gate, 0, 1.0f);
        put_f32(gate_in, 0, 2.0f);
        ext_setup(TSIR_BODY_SILU_GATED, roles, 3, 3, 1);
        ext_binding(0, 1, TSIR_SCALAR_F32, false);
        ext_binding(1, 1, TSIR_SCALAR_F32, false);
        ext_binding(2, 1, TSIR_SCALAR_F32, true);
        inputs[0].data = gate;
        inputs[0].len = 4;
        inputs[1].data = gate_in;
        inputs[1].len = 4;
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 2, &result);
        CHECK_STATUS(status, TSIR_OK);
        if (status == TSIR_OK) {
            check_f32_near(result.outputs[0], 0, 1.4621172f,
                0.000001f);
            tsir_reference_free_result(&result);
        }
    }
    /* Gated gelu exact case: gelu(0) = 0. */
    {
        static const TsirSemanticBindingRole roles[3] = {
            TSIR_ROLE_GATE, TSIR_ROLE_INPUT, TSIR_ROLE_OUTPUT
        };
        put_f32(gate, 0, 0.0f);
        put_f32(gate_in, 0, 5.0f);
        ext_setup(TSIR_BODY_GELU_GATED, roles, 3, 3, 1);
        ext_binding(0, 1, TSIR_SCALAR_F32, false);
        ext_binding(1, 1, TSIR_SCALAR_F32, false);
        ext_binding(2, 1, TSIR_SCALAR_F32, true);
        inputs[0].data = gate;
        inputs[0].len = 4;
        inputs[1].data = gate_in;
        inputs[1].len = 4;
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 2, &result);
        CHECK_STATUS(status, TSIR_OK);
        if (status == TSIR_OK) {
            check_f32_near(result.outputs[0], 0, 0.0f, 0.0f);
            tsir_reference_free_result(&result);
        }
    }
    /* Attention scores, single-Q decode, tolerance only. */
    {
        static const TsirSemanticBindingRole roles[4] = {
            TSIR_ROLE_QUERY, TSIR_ROLE_KEY, TSIR_ROLE_VALUE,
            TSIR_ROLE_OUTPUT
        };
        static uint64_t q_shape[1] = { 2 };
        static uint64_t k_shape[2] = { 2, 2 };
        static uint64_t v_shape[2] = { 2, 2 };
        static uint64_t o_shape[1] = { 2 };
        uint8_t q[8];
        uint8_t k[16];
        uint8_t v[16];
        static const float qv[2] = { 1.0f, 0.0f };
        static const float kv[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        static const float vv[4] = { 10.0f, 20.0f, 30.0f, 40.0f };
        put_f32s(q, qv, 2);
        put_f32s(k, kv, 4);
        put_f32s(v, vv, 4);
        ext_setup(TSIR_BODY_ATTENTION_SCORES, roles, 4, 4, 0);
        ext_bindings[0].name = "q";
        ext_bindings[0].logical_shape = q_shape;
        ext_bindings[0].logical_shape_len = 1;
        ext_bindings[0].elem = TSIR_SCALAR_F32;
        ext_bindings[1].name = "k";
        ext_bindings[1].logical_shape = k_shape;
        ext_bindings[1].logical_shape_len = 2;
        ext_bindings[1].elem = TSIR_SCALAR_F32;
        ext_bindings[2].name = "v";
        ext_bindings[2].logical_shape = v_shape;
        ext_bindings[2].logical_shape_len = 2;
        ext_bindings[2].elem = TSIR_SCALAR_F32;
        ext_bindings[3].name = "o";
        ext_bindings[3].logical_shape = o_shape;
        ext_bindings[3].logical_shape_len = 1;
        ext_bindings[3].elem = TSIR_SCALAR_F32;
        ext_bindings[3].read_write = true;
        ext_func.body.has_attention_scores = true;
        ext_func.body.attention_scores.softmax_mode =
            TSIR_SOFTMAX_TWO_PASS_STABLE;
        ext_func.body.attention_scores.head_dim = 2;
        ext_func.body.attention_scores.scale_source =
            TSIR_SCALE_LITERAL_F32;
        ext_func.body.attention_scores.has_scale_literal_f64 = true;
        ext_func.body.attention_scores.scale_literal_f64 = 1.0;
        ext_func.body.attention_scores.has_softcap = false;
        ext_func.body.attention_scores.causal_mode =
            TSIR_CAUSAL_NONE;
        inputs[0].data = q;
        inputs[0].len = 8;
        inputs[1].data = k;
        inputs[1].len = 16;
        inputs[2].data = v;
        inputs[2].len = 16;
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 3, &result);
        CHECK_STATUS(status, TSIR_OK);
        if (status == TSIR_OK) {
            check_f32_near(result.outputs[0], 0, 15.378829f,
                0.00001f);
            check_f32_near(result.outputs[0], 1, 25.378829f,
                0.00001f);
            tsir_reference_free_result(&result);
        }
    }
    /* L2 normalize [3,4] with eps 0: [0.6, 0.8], tolerance only
     * (sqrt is double-rounded, not claimed Zig-bit-equal). */
    {
        static const TsirSemanticBindingRole roles[2] = {
            TSIR_ROLE_INPUT, TSIR_ROLE_OUTPUT
        };
        uint8_t in[8];
        static const float iv[2] = { 3.0f, 4.0f };
        put_f32s(in, iv, 2);
        ext_setup(TSIR_BODY_L2_NORMALIZE, roles, 2, 2, 0);
        ext_binding(0, 2, TSIR_SCALAR_F32, false);
        ext_binding(1, 5, TSIR_SCALAR_F32, true);
        ext_func.body.has_l2_normalize = true;
        ext_func.body.l2_normalize.hidden = 2;
        ext_func.body.l2_normalize.eps = 0.0f;
        inputs[0].data = in;
        inputs[0].len = 8;
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 1, &result);
        CHECK_STATUS(status, TSIR_OK);
        if (status == TSIR_OK) {
            check_f32_near(result.outputs[0], 0, 0.6f, 0.000001f);
            check_f32_near(result.outputs[0], 1, 0.8f, 0.000001f);
            tsir_reference_free_result(&result);
        }
    }
    /* Conv1D depthwise, channels 1, kernel 2, no bias. */
    {
        static const TsirSemanticBindingRole roles[3] = {
            TSIR_ROLE_INPUT, TSIR_ROLE_WEIGHT, TSIR_ROLE_OUTPUT
        };
        uint8_t in[12];
        uint8_t w[8];
        static const float iv[3] = { 1.0f, 2.0f, 3.0f };
        static const float wv[2] = { 10.0f, 20.0f };
        put_f32s(in, iv, 3);
        put_f32s(w, wv, 2);
        ext_setup(TSIR_BODY_CONV1D_DEPTHWISE, roles, 3, 3, 0);
        ext_binding(0, 3, TSIR_SCALAR_F32, false);
        ext_binding(1, 2, TSIR_SCALAR_F32, false);
        ext_binding(2, 3, TSIR_SCALAR_F32, true);
        ext_func.body.has_conv1d_depthwise = true;
        ext_func.body.conv1d_depthwise.channels = 1;
        ext_func.body.conv1d_depthwise.kernel_size = 2;
        ext_func.body.conv1d_depthwise.has_bias = false;
        inputs[0].data = in;
        inputs[0].len = 12;
        inputs[1].data = w;
        inputs[1].len = 8;
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 2, &result);
        CHECK_STATUS(status, TSIR_OK);
        if (status == TSIR_OK) {
            check_f32_near(result.outputs[0], 0, 20.0f, 0.0f);
            check_f32_near(result.outputs[0], 1, 50.0f, 0.0f);
            check_f32_near(result.outputs[0], 2, 80.0f, 0.0f);
            tsir_reference_free_result(&result);
        }
    }
}

static void test_linear_attention_step_and_fallthrough(void) {
    float q[1] = { 2.0f };
    float k[1] = { 1.0f };
    float v[1] = { 3.0f };
    float gate[1] = { 0.0f };
    float state_in[1] = { 1.0f };
    float state_out[1] = { 0.0f };
    float out[1] = { 0.0f };
    TsirStatus status = TSIR_OK;
    status = tsir_linear_attention_step(q, k, v, gate, state_in,
        1, 1, state_out, out);
    CHECK_STATUS(status, TSIR_OK);
    CHECK(near_f32(state_out[0], 1.0f, 0.000001f));
    CHECK(near_f32(out[0], 4.0f, 0.000001f));
    /* Dispatch fall-through: the Zig path reads its read-write
     * state through the read-only input helper and never matches. */
    {
        static const TsirSemanticBindingRole roles[6] = {
            TSIR_ROLE_QUERY, TSIR_ROLE_KEY, TSIR_ROLE_VALUE,
            TSIR_ROLE_GATE, TSIR_ROLE_LINEAR_STATE, TSIR_ROLE_OUTPUT
        };
        TsirInputBuffer inputs[4];
        TsirReferenceResult result;
        TsirRealization realization;
        uint8_t qb[4];
        uint8_t kb[4];
        uint8_t vb[4];
        uint8_t gb[4];
        memset(&realization, 0, sizeof(realization));
        put_f32(qb, 0, 2.0f);
        put_f32(kb, 0, 1.0f);
        put_f32(vb, 0, 3.0f);
        put_f32(gb, 0, 0.0f);
        ext_setup(TSIR_BODY_LINEAR_ATTENTION, roles, 6, 6, 0);
        ext_binding(0, 1, TSIR_SCALAR_F32, false);
        ext_binding(1, 1, TSIR_SCALAR_F32, false);
        ext_binding(2, 1, TSIR_SCALAR_F32, false);
        ext_binding(3, 1, TSIR_SCALAR_F32, false);
        ext_binding(4, 1, TSIR_SCALAR_F32, true);
        ext_binding(5, 1, TSIR_SCALAR_F32, true);
        ext_func.body.has_linear_attention = true;
        ext_func.body.linear_attention.key_dim = 1;
        ext_func.body.linear_attention.value_dim = 1;
        ext_func.body.linear_attention.norm_mode =
            TSIR_LINEAR_NORM_SHARED;
        ext_func.body.linear_attention.has_dt_bias = false;
        inputs[0].data = qb;
        inputs[0].len = 4;
        inputs[1].data = kb;
        inputs[1].len = 4;
        inputs[2].data = vb;
        inputs[2].len = 4;
        inputs[3].data = gb;
        inputs[3].len = 4;
        memset(&result, 0, sizeof(result));
        status = tsir_reference_run(&ext_semantic, &realization,
            inputs, 4, &result);
        CHECK_STATUS(status, TSIR_ERR_UNSUPPORTED);
    }
}

static void test_transcendentals(void) {
    CHECK(tsir_exp_f32(0.0f) == 1.0f);
    CHECK(tsir_tanh_f32(0.0f) == 0.0f);
    CHECK(tsir_sqrt_f32(4.0f) == 2.0f);
    CHECK(near_f32(tsir_exp_f32(1.0f), 2.7182818f, 0.000001f));
    CHECK(near_f32(tsir_tanh_f32(1.0f), 0.7615942f, 0.000001f));
    CHECK(near_f32(tsir_sqrt_f32(2.0f), 1.4142135f, 0.000001f));
}


int main(void) {
    test_frontend_layout_and_query();
    test_frontend_bindings();
    test_frontend_for_axis_and_rejection();
    test_extended_identity_residual();
    test_extended_gated_attention_l2_conv();
    test_linear_attention_step_and_fallthrough();
    test_transcendentals();
    return finish_tests("test_new");
}
