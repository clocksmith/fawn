/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_analyze_internal.h"

#include <string.h>

static bool name_equals(const char *stored, const char *text,
    size_t len) {
    return strlen(stored) == len && memcmp(stored, text, len) == 0;
}

bool dxil_parse_find_param(DxilAnalyzeParser *parser,
    const char *text, size_t len, uint32_t *out_index) {
    uint32_t i = 0;
    for (i = 0; i < parser->function->param_count; i++) {
        if (name_equals(parser->function->params[i].name, text, len)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

bool dxil_parse_find_local(DxilAnalyzeParser *parser, const char *text,
    size_t len, uint32_t *out_index) {
    uint32_t i = 0;
    for (i = 0; i < parser->function->local_count; i++) {
        if (name_equals(parser->function->locals[i].name, text, len)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

bool dxil_parse_find_global(DxilAnalyzeParser *parser, const char *text,
    size_t len, uint32_t *out_index) {
    uint32_t i = 0;
    for (i = 0; i < parser->module->global_count; i++) {
        if (name_equals(parser->module->globals[i].name, text, len)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

bool dxil_parse_add_pending(DxilAnalyzeParser *parser,
    uint32_t function_index, DxilIrExprId expr_id, const char *name,
    size_t name_len, size_t offset) {
    DxilAnalyzePending *pending = NULL;
    if (parser->pending_count >= DXIL_ANALYZE_MAX_PENDING) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    pending = &parser->pending[parser->pending_count];
    pending->function_index = function_index;
    pending->expr_id = expr_id;
    pending->name = name;
    pending->name_len = name_len;
    pending->offset = offset;
    parser->pending_count += 1;
    return true;
}

bool dxil_parse_add_member_pending(DxilAnalyzeParser *parser,
    uint32_t function_index, DxilIrExprId expr_id, const char *name,
    size_t name_len, size_t offset) {
    DxilAnalyzePending *pending = NULL;
    if (parser->member_pending_count >= DXIL_ANALYZE_MAX_PENDING) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    pending = &parser->member_pending[parser->member_pending_count];
    pending->function_index = function_index;
    pending->expr_id = expr_id;
    pending->name = name;
    pending->name_len = name_len;
    pending->offset = offset;
    parser->member_pending_count += 1;
    return true;
}

static bool parse_struct_decl(DxilAnalyzeParser *parser,
    uint32_t attr_count) {
    const char *name_text = NULL;
    size_t name_len = 0;
    const char *name = NULL;
    DxilIrStructId struct_id = DXIL_IR_INVALID_ID;
    uint32_t i = 0;
    if (attr_count > 0) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    dxil_parse_advance(parser);
    if (!dxil_parse_expect_ident(parser, &name_text, &name_len,
            NULL)) {
        return false;
    }
    if (dxil_parse_find_struct(parser, name_text, name_len) >= 0) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_DUPLICATE_SYMBOL,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    name = dxil_parse_dup_name(parser, name_text, name_len);
    if (name == NULL) {
        return false;
    }
    if (dxil_ir_add_struct(parser->module, name, &struct_id)
        != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    if (!dxil_parse_expect_punct(parser, '{')) {
        return false;
    }
    while (!dxil_parse_is_punct(parser, '}')) {
        DxilAnalyzeAttr field_attrs[DXIL_ANALYZE_MAX_ATTRS];
        uint32_t field_attr_count = 0;
        const char *field_text = NULL;
        size_t field_len = 0;
        const char *field_name = NULL;
        DxilIrTypeId field_ty = DXIL_IR_INVALID_ID;
        DxilIrStructField field;
        if (parser->tok.kind == DXIL_TOK_EOF) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                DXIL_DIAG_STAGE_PARSER);
            return false;
        }
        if (!dxil_parse_raw_attrs(parser, field_attrs,
                &field_attr_count)) {
            return false;
        }
        if (!dxil_parse_expect_ident(parser, &field_text, &field_len,
                NULL)) {
            return false;
        }
        for (i = 0;
            i < parser->module->structs[struct_id].field_count;
            i++) {
            if (name_equals(
                    parser->module->structs[struct_id].fields[i].name,
                    field_text, field_len)) {
                dxil_parse_fail_tok(parser, DXIL_DIAG_DUPLICATE_SYMBOL,
                    DXIL_DIAG_STAGE_SEMA);
                return false;
            }
        }
        field_name = dxil_parse_dup_name(parser, field_text,
            field_len);
        if (field_name == NULL) {
            return false;
        }
        if (!dxil_parse_expect_punct(parser, ':')) {
            return false;
        }
        if (!dxil_parse_type(parser, &field_ty)) {
            return false;
        }
        memset(&field, 0, sizeof(field));
        field.name = field_name;
        field.ty = field_ty;
        if (!dxil_parse_interpret_io_attrs(parser, field_attrs,
                field_attr_count, &field.io, &field.has_io)) {
            return false;
        }
        if (dxil_ir_add_struct_field(parser->module, struct_id,
                &field) != DXIL_OK) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
                DXIL_DIAG_STAGE_IR_BUILDER);
            return false;
        }
        dxil_parse_accept_punct(parser, ',');
    }
    dxil_parse_advance(parser);
    dxil_parse_accept_punct(parser, ';');
    return true;
}

static bool parse_global_decl(DxilAnalyzeParser *parser,
    const DxilAnalyzeAttr *attrs, uint32_t attr_count) {
    DxilIrGlobalClass class = DXIL_IR_GLOBAL_VAR;
    DxilIrGlobal global;
    const char *name_text = NULL;
    size_t name_len = 0;
    size_t name_offset = 0;
    const char *name = NULL;
    DxilIrTypeId type_id = DXIL_IR_INVALID_ID;
    DxilIrType type;
    bool duplicate = false;
    const DxilAnalyzeAttr *group_attr = NULL;
    const DxilAnalyzeAttr *binding_attr = NULL;
    const DxilAnalyzeAttr *id_attr = NULL;
    uint32_t i = 0;
    memset(&global, 0, sizeof(global));
    if (dxil_parse_is_kw(parser, "var")) {
        class = DXIL_IR_GLOBAL_VAR;
    } else if (dxil_parse_is_kw(parser, "const")) {
        class = DXIL_IR_GLOBAL_CONST;
    } else {
        class = DXIL_IR_GLOBAL_OVERRIDE;
    }
    group_attr = dxil_parse_find_attr(attrs, attr_count, "group",
        &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    binding_attr = dxil_parse_find_attr(attrs, attr_count, "binding",
        &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    id_attr = dxil_parse_find_attr(attrs, attr_count, "id",
        &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    for (i = 0; i < attr_count; i++) {
        if (&attrs[i] != group_attr && &attrs[i] != binding_attr
            && &attrs[i] != id_attr) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, attrs[i].offset);
            return false;
        }
    }
    if ((group_attr != NULL) != (binding_attr != NULL)) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    if (group_attr != NULL
        && (group_attr->int_count != 1
            || group_attr->int_values[0] > UINT32_MAX)) {
        dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA, group_attr->offset);
        return false;
    }
    if (binding_attr != NULL
        && (binding_attr->int_count != 1
            || binding_attr->int_values[0] > UINT32_MAX)) {
        dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA, binding_attr->offset);
        return false;
    }
    if (id_attr != NULL
        && (id_attr->int_count != 1
            || id_attr->int_values[0] > UINT32_MAX)) {
        dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA, id_attr->offset);
        return false;
    }
    dxil_parse_advance(parser);
    global.class = class;
    if (dxil_parse_accept_punct(parser, '<')) {
        for (;;) {
            const char *arg_text = NULL;
            size_t arg_len = 0;
            if (!dxil_parse_expect_ident(parser, &arg_text, &arg_len,
                    NULL)) {
                return false;
            }
            if (arg_len == 8
                && memcmp(arg_text, "function", 8) == 0) {
                if (global.has_addr_space) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_addr_space = true;
                global.addr_space = DXIL_IR_ADDR_FUNCTION;
            } else if (arg_len == 7
                && memcmp(arg_text, "private", 7) == 0) {
                if (global.has_addr_space) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_addr_space = true;
                global.addr_space = DXIL_IR_ADDR_PRIVATE;
            } else if (arg_len == 9
                && memcmp(arg_text, "workgroup", 9) == 0) {
                if (global.has_addr_space) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_addr_space = true;
                global.addr_space = DXIL_IR_ADDR_WORKGROUP;
            } else if (arg_len == 7
                && memcmp(arg_text, "uniform", 7) == 0) {
                if (global.has_addr_space) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_addr_space = true;
                global.addr_space = DXIL_IR_ADDR_UNIFORM;
            } else if (arg_len == 7
                && memcmp(arg_text, "storage", 7) == 0) {
                if (global.has_addr_space) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_addr_space = true;
                global.addr_space = DXIL_IR_ADDR_STORAGE;
            } else if (arg_len == 6
                && memcmp(arg_text, "handle", 6) == 0) {
                if (global.has_addr_space) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_addr_space = true;
                global.addr_space = DXIL_IR_ADDR_HANDLE;
            } else if (arg_len == 4
                && memcmp(arg_text, "read", 4) == 0) {
                if (global.has_access) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_access = true;
                global.access = DXIL_IR_ACCESS_READ;
            } else if (arg_len == 5
                && memcmp(arg_text, "write", 5) == 0) {
                if (global.has_access) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_access = true;
                global.access = DXIL_IR_ACCESS_WRITE;
            } else if (arg_len == 10
                && memcmp(arg_text, "read_write", 10) == 0) {
                if (global.has_access) {
                    dxil_parse_fail_tok(parser,
                        DXIL_DIAG_INVALID_ATTRIBUTE,
                        DXIL_DIAG_STAGE_SEMA);
                    return false;
                }
                global.has_access = true;
                global.access = DXIL_IR_ACCESS_READ_WRITE;
            } else {
                dxil_parse_fail_tok(parser,
                    DXIL_DIAG_INVALID_ATTRIBUTE,
                    DXIL_DIAG_STAGE_SEMA);
                return false;
            }
            if (!dxil_parse_accept_punct(parser, ',')) {
                break;
            }
        }
        if (!dxil_parse_expect_punct(parser, '>')) {
            return false;
        }
    }
    if (!dxil_parse_expect_ident(parser, &name_text, &name_len,
            &name_offset)) {
        return false;
    }
    if (dxil_parse_find_global(parser, name_text, name_len, &i)) {
        dxil_parse_fail_at(parser, DXIL_DIAG_DUPLICATE_SYMBOL,
            DXIL_DIAG_STAGE_SEMA, name_offset);
        return false;
    }
    name = dxil_parse_dup_name(parser, name_text, name_len);
    if (name == NULL) {
        return false;
    }
    global.name = name;
    if (!dxil_parse_expect_punct(parser, ':')) {
        return false;
    }
    if (!dxil_parse_type(parser, &type_id)) {
        return false;
    }
    global.ty = type_id;
    if (dxil_parse_accept_punct(parser, '=')) {
        if (parser->tok.kind == DXIL_TOK_INT) {
            global.has_initializer = true;
            global.init_kind = DXIL_IR_CONST_INT;
            global.init_int = parser->tok.int_value;
            dxil_parse_advance(parser);
        } else if (parser->tok.kind == DXIL_TOK_FLOAT) {
            global.has_initializer = true;
            global.init_kind = DXIL_IR_CONST_FLOAT;
            global.init_float = parser->tok.float_value;
            dxil_parse_advance(parser);
        } else if (dxil_parse_is_kw(parser, "true")
            || dxil_parse_is_kw(parser, "false")) {
            global.has_initializer = true;
            global.init_kind = DXIL_IR_CONST_BOOL;
            global.init_bool = dxil_parse_is_kw(parser, "true");
            dxil_parse_advance(parser);
        } else {
            dxil_parse_fail_tok(parser,
                DXIL_DIAG_UNSUPPORTED_CONSTRUCT,
                DXIL_DIAG_STAGE_SEMA);
            return false;
        }
    }
    if (!dxil_parse_expect_punct(parser, ';')) {
        return false;
    }
    if (group_attr != NULL) {
        global.has_binding = true;
        global.binding.group =
            (uint32_t)group_attr->int_values[0];
        global.binding.binding =
            (uint32_t)binding_attr->int_values[0];
    }
    if (!global.has_addr_space) {
        if (dxil_ir_type_get(parser->module, type_id, &type)
            == DXIL_OK
            && (type.kind == DXIL_IR_TYPE_SAMPLER
                || type.kind == DXIL_IR_TYPE_SAMPLER_COMPARISON
                || type.kind == DXIL_IR_TYPE_TEXTURE_1D
                || type.kind == DXIL_IR_TYPE_TEXTURE_2D
                || type.kind == DXIL_IR_TYPE_TEXTURE_2D_ARRAY
                || type.kind == DXIL_IR_TYPE_TEXTURE_CUBE
                || type.kind == DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D
                || type.kind == DXIL_IR_TYPE_TEXTURE_DEPTH_2D
                || type.kind == DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE
                || type.kind == DXIL_IR_TYPE_TEXTURE_3D
                || type.kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D)) {
            global.has_addr_space = true;
            global.addr_space = DXIL_IR_ADDR_HANDLE;
        }
    }
    {
        DxilIrGlobalId global_id = DXIL_IR_INVALID_ID;
        if (dxil_ir_add_global(parser->module, &global, &global_id)
            != DXIL_OK) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
                DXIL_DIAG_STAGE_IR_BUILDER);
            return false;
        }
    }
    return true;
}

static bool parse_function_decl(DxilAnalyzeParser *parser,
    const DxilAnalyzeAttr *attrs, uint32_t attr_count) {
    const char *name_text = NULL;
    size_t name_len = 0;
    size_t name_offset = 0;
    const char *name = NULL;
    DxilIrFunction *function = NULL;
    uint32_t function_index = 0;
    DxilIrTypeId return_type = DXIL_IR_INVALID_ID;
    DxilIrIoAttr return_io;
    bool has_return_io = false;
    bool has_stage = false;
    DxilIrStage stage = DXIL_IR_STAGE_COMPUTE;
    uint32_t workgroup_size[3] = { 1, 1, 1 };
    bool duplicate = false;
    const DxilAnalyzeAttr *compute_attr = NULL;
    const DxilAnalyzeAttr *vertex_attr = NULL;
    const DxilAnalyzeAttr *fragment_attr = NULL;
    const DxilAnalyzeAttr *workgroup_attr = NULL;
    uint32_t i = 0;
    uint32_t j = 0;
    compute_attr = dxil_parse_find_attr(attrs, attr_count, "compute",
        &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    vertex_attr = dxil_parse_find_attr(attrs, attr_count, "vertex",
        &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    fragment_attr = dxil_parse_find_attr(attrs, attr_count,
        "fragment", &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    workgroup_attr = dxil_parse_find_attr(attrs, attr_count,
        "workgroup_size", &duplicate);
    if (duplicate) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    for (i = 0; i < attr_count; i++) {
        if (&attrs[i] != compute_attr && &attrs[i] != vertex_attr
            && &attrs[i] != fragment_attr
            && &attrs[i] != workgroup_attr) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, attrs[i].offset);
            return false;
        }
    }
    if ((compute_attr != NULL) + (vertex_attr != NULL)
        + (fragment_attr != NULL) > 1) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
            DXIL_DIAG_STAGE_SEMA);
        return false;
    }
    if (compute_attr != NULL) {
        has_stage = true;
        stage = DXIL_IR_STAGE_COMPUTE;
    } else if (vertex_attr != NULL) {
        has_stage = true;
        stage = DXIL_IR_STAGE_VERTEX;
    } else if (fragment_attr != NULL) {
        has_stage = true;
        stage = DXIL_IR_STAGE_FRAGMENT;
    }
    if (workgroup_attr != NULL) {
        if (workgroup_attr->int_count < 1
            || workgroup_attr->int_count > 3
            || !has_stage || stage != DXIL_IR_STAGE_COMPUTE) {
            dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_ATTRIBUTE,
                DXIL_DIAG_STAGE_SEMA, workgroup_attr->offset);
            return false;
        }
        for (j = 0; j < workgroup_attr->int_count; j++) {
            if (workgroup_attr->int_values[j] > UINT32_MAX) {
                dxil_parse_fail_at(parser, DXIL_DIAG_INVALID_WGSL,
                    DXIL_DIAG_STAGE_PARSER, workgroup_attr->offset);
                return false;
            }
            workgroup_size[j] =
                (uint32_t)workgroup_attr->int_values[j];
        }
    }
    dxil_parse_advance(parser);
    if (!dxil_parse_expect_ident(parser, &name_text, &name_len,
            &name_offset)) {
        return false;
    }
    for (i = 0; i < parser->module->function_count; i++) {
        if (name_equals(parser->module->functions[i].name, name_text,
                name_len)) {
            dxil_parse_fail_at(parser, DXIL_DIAG_DUPLICATE_SYMBOL,
                DXIL_DIAG_STAGE_SEMA, name_offset);
            return false;
        }
    }
    name = dxil_parse_dup_name(parser, name_text, name_len);
    if (name == NULL) {
        return false;
    }
    return_type = parser->void_ty;
    if (dxil_ir_add_function(parser->module, name, return_type,
            &function) != DXIL_OK) {
        dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
            DXIL_DIAG_STAGE_IR_BUILDER);
        return false;
    }
    function_index = parser->module->function_count - 1;
    function->name = name;
    function->return_type = return_type;
    function->has_stage = has_stage;
    function->stage = stage;
    function->workgroup_size[0] = workgroup_size[0];
    function->workgroup_size[1] = workgroup_size[1];
    function->workgroup_size[2] = workgroup_size[2];
    parser->function = function;
    parser->function_index = function_index;
    if (!dxil_parse_expect_punct(parser, '(')) {
        return false;
    }
    while (!dxil_parse_is_punct(parser, ')')) {
        DxilAnalyzeAttr param_attrs[DXIL_ANALYZE_MAX_ATTRS];
        uint32_t param_attr_count = 0;
        const char *param_text = NULL;
        size_t param_len = 0;
        const char *param_name = NULL;
        DxilIrTypeId param_ty = DXIL_IR_INVALID_ID;
        DxilIrParam *param = NULL;
        if (parser->tok.kind == DXIL_TOK_EOF) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
                DXIL_DIAG_STAGE_PARSER);
            return false;
        }
        if (!dxil_parse_raw_attrs(parser, param_attrs,
                &param_attr_count)) {
            return false;
        }
        if (!dxil_parse_expect_ident(parser, &param_text, &param_len,
                NULL)) {
            return false;
        }
        for (i = 0; i < function->param_count; i++) {
            if (name_equals(function->params[i].name, param_text,
                    param_len)) {
                dxil_parse_fail_tok(parser,
                    DXIL_DIAG_DUPLICATE_SYMBOL,
                    DXIL_DIAG_STAGE_SEMA);
                return false;
            }
        }
        param_name = dxil_parse_dup_name(parser, param_text,
            param_len);
        if (param_name == NULL) {
            return false;
        }
        if (!dxil_parse_expect_punct(parser, ':')) {
            return false;
        }
        if (!dxil_parse_type(parser, &param_ty)) {
            return false;
        }
        if (function->param_count >= DXIL_IR_MAX_PARAMS) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
                DXIL_DIAG_STAGE_IR_BUILDER);
            return false;
        }
        param = &function->params[function->param_count];
        memset(param, 0, sizeof(*param));
        param->name = param_name;
        param->ty = param_ty;
        if (!dxil_parse_interpret_io_attrs(parser, param_attrs,
                param_attr_count, &param->io, &param->has_io)) {
            return false;
        }
        function->param_count += 1;
        if (!dxil_parse_accept_punct(parser, ',')) {
            break;
        }
    }
    if (!dxil_parse_expect_punct(parser, ')')) {
        return false;
    }
    if (dxil_parse_accept_punct(parser, DXIL_P_ARROW)) {
        DxilAnalyzeAttr ret_attrs[DXIL_ANALYZE_MAX_ATTRS];
        uint32_t ret_attr_count = 0;
        if (!dxil_parse_raw_attrs(parser, ret_attrs,
                &ret_attr_count)) {
            return false;
        }
        if (!dxil_parse_type(parser, &return_type)) {
            return false;
        }
        function->return_type = return_type;
        if (!dxil_parse_interpret_io_attrs(parser, ret_attrs,
                ret_attr_count, &return_io, &has_return_io)) {
            return false;
        }
        function->has_return_io = has_return_io;
        function->return_io = return_io;
    }
    {
        DxilIrStmtId root = DXIL_IR_INVALID_ID;
        if (!dxil_parse_braced_block(parser, &root)) {
            return false;
        }
        function->root_stmt = root;
    }
    if (has_stage) {
        DxilIrEntryPoint entry;
        memset(&entry, 0, sizeof(entry));
        entry.function = function_index;
        entry.stage = stage;
        entry.workgroup_size[0] = workgroup_size[0];
        entry.workgroup_size[1] = workgroup_size[1];
        entry.workgroup_size[2] = workgroup_size[2];
        if (dxil_ir_add_entry_point(parser->module, &entry)
            != DXIL_OK) {
            dxil_parse_fail_tok(parser, DXIL_DIAG_OUTPUT_TOO_LARGE,
                DXIL_DIAG_STAGE_IR_BUILDER);
            return false;
        }
    }
    parser->function = NULL;
    return true;
}

bool dxil_parse_decl(DxilAnalyzeParser *parser) {
    DxilAnalyzeAttr attrs[DXIL_ANALYZE_MAX_ATTRS];
    uint32_t attr_count = 0;
    if (!dxil_parse_raw_attrs(parser, attrs, &attr_count)) {
        return false;
    }
    if (dxil_parse_is_kw(parser, "struct")) {
        return parse_struct_decl(parser, attr_count);
    }
    if (dxil_parse_is_kw(parser, "var")
        || dxil_parse_is_kw(parser, "const")
        || dxil_parse_is_kw(parser, "override")) {
        return parse_global_decl(parser, attrs, attr_count);
    }
    if (dxil_parse_is_kw(parser, "fn")) {
        return parse_function_decl(parser, attrs, attr_count);
    }
    dxil_parse_fail_tok(parser, DXIL_DIAG_UNEXPECTED_TOKEN,
        DXIL_DIAG_STAGE_PARSER);
    return false;
}
