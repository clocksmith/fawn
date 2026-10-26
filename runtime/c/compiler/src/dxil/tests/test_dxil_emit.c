/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_util.h"

#include <stdio.h>

#include "dxil_diagnostic.h"
#include "dxil_emit.h"
#include "dxil_ir.h"
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

static void build_canonical(DxilIrModule *module) {
    DxilIrFunction *function = NULL;
    DxilIrTypeId void_ty = scalar_ty(module, DXIL_IR_SCALAR_VOID);
    DxilIrTypeId u32_ty = scalar_ty(module, DXIL_IR_SCALAR_U32);
    DxilIrType vec3;
    DxilIrTypeId vec3_ty = DXIL_IR_INVALID_ID;
    DxilIrStmt block;
    DxilIrStmtId block_id = DXIL_IR_INVALID_ID;
    DxilIrEntryPoint entry;
    memset(&vec3, 0, sizeof(vec3));
    vec3.kind = DXIL_IR_TYPE_VECTOR;
    vec3.elem = u32_ty;
    vec3.len = 3;
    dxil_ir_intern_type(module, &vec3, &vec3_ty);
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

static void check_lower(void) {
    DxilIrModule module;
    DxilLoweredModule lowered;
    DxilIrModule empty;
    dxil_ir_init(&module);
    build_canonical(&module);
    CHECK_STATUS(dxil_lower(&module, &lowered), DXIL_OK);
    CHECK(lowered.stage == DXIL_STAGE_COMPUTE);
    CHECK(strcmp(lowered.name, "main") == 0);
    CHECK(lowered.workgroup_size[0] == 64
        && lowered.workgroup_size[1] == 1
        && lowered.workgroup_size[2] == 1);
    CHECK(lowered.shader_model_major == 6
        && lowered.shader_model_minor == 0);
    dxil_ir_init(&empty);
    CHECK_STATUS(dxil_lower(&empty, &lowered), DXIL_INVALID_IR);
}

static void check_emit(void) {
    DxilIrModule module;
    static uint8_t out[65536];
    size_t written = 0;
    DxilDiagnostic diagnostic;
    DxilValidationResult result;
    dxil_ir_init(&module);
    build_canonical(&module);
    dxil_diagnostic_clear(&diagnostic);
    CHECK_STATUS(dxil_emit_with_diagnostic(&module, out,
        sizeof(out), &written, &diagnostic), DXIL_OK);
    CHECK(written > 32);
    CHECK(memcmp(out, "DXBC", 4) == 0);
    dxil_validate(out, written, &result);
    CHECK(result.valid);
    CHECK(result.has_shader_kind && result.shader_kind == 5);
    {
        uint8_t tiny[16];
        size_t tiny_written = 0;
        dxil_diagnostic_clear(&diagnostic);
        CHECK_STATUS(dxil_emit_with_diagnostic(&module, tiny,
            sizeof(tiny), &tiny_written, &diagnostic),
            DXIL_OUTPUT_TOO_LARGE);
    }
    {
        DxilIrModule empty;
        dxil_ir_init(&empty);
        dxil_diagnostic_clear(&diagnostic);
        CHECK_STATUS(dxil_emit_with_diagnostic(&empty, out,
            sizeof(out), &written, &diagnostic), DXIL_INVALID_IR);
        CHECK(diagnostic.message_len == 0);
    }
}

static void check_diagnostic_units(void) {
    DxilDiagnostic diagnostic;
    DxilDiagKind kind = DXIL_DIAG_INVALID_IR;
    const char *source = "line one\nline two is here\nline three\n";
    dxil_diagnostic_clear(&diagnostic);
    CHECK(diagnostic.message_len == 0);
    CHECK(diagnostic.stage == DXIL_DIAG_STAGE_NONE);
    dxil_diagnostic_set(&diagnostic, DXIL_DIAG_STAGE_SEMA,
        DXIL_DIAG_UNKNOWN_IDENTIFIER, source, strlen(source), 12);
    CHECK(diagnostic.has_kind);
    CHECK(diagnostic.kind == DXIL_DIAG_UNKNOWN_IDENTIFIER);
    CHECK(diagnostic.line == 2);
    CHECK(diagnostic.column == 4);
    CHECK(strstr(diagnostic.message, "sema: UnknownIdentifier")
        != NULL);
    CHECK(strstr(diagnostic.message, "2:4") != NULL);
    CHECK(strstr(diagnostic.message, "line two is here") != NULL);
    dxil_diagnostic_set_detail(&diagnostic, DXIL_DIAG_STAGE_NONE,
        DXIL_DIAG_INVALID_IR, "inner detail");
    CHECK(strstr(diagnostic.message, "none: InvalidIr: inner detail")
        != NULL);
    CHECK(dxil_diagnostic_kind_from_status(DXIL_UNSUPPORTED_CONSTRUCT,
        &kind) && kind == DXIL_DIAG_UNSUPPORTED_CONSTRUCT);
    CHECK(dxil_diagnostic_kind_from_status(DXIL_OUTPUT_TOO_LARGE,
        &kind) && kind == DXIL_DIAG_OUTPUT_TOO_LARGE);
    CHECK(!dxil_diagnostic_kind_from_status(DXIL_OK, &kind));
    CHECK(strcmp(dxil_diagnostic_stage_name(
        DXIL_DIAG_STAGE_DXIL_EMIT), "dxil_emit") == 0);
    CHECK(strcmp(dxil_diagnostic_kind_name(
        DXIL_DIAG_SHADER_TOOLCHAIN_UNAVAILABLE),
        "ShaderToolchainUnavailable") == 0);
}

static void check_diagnostic_context_boundary(void) {
    DxilDiagnostic diagnostic;
    char source[DXIL_DIAGNOSTIC_CONTEXT_CAP * 2u];
    const size_t lengths[] = {DXIL_DIAGNOSTIC_CONTEXT_CAP - 1u,
        DXIL_DIAGNOSTIC_CONTEXT_CAP, sizeof(source)};
    const size_t offsets[] = {0u, 48u, sizeof(source) - 1u};
    for (size_t i = 0; i < sizeof(source); i++) {
        source[i] = (char)('a' + i % 26u);
    }
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        size_t length = lengths[i];
        size_t expected_len = length < DXIL_DIAGNOSTIC_CONTEXT_CAP
            ? length : DXIL_DIAGNOSTIC_CONTEXT_CAP;
        size_t start = i == 2u ? length - expected_len : 0u;
        char expected[DXIL_DIAGNOSTIC_MESSAGE_CAP];
        memset(&diagnostic, 0x7f, sizeof(diagnostic));
        dxil_diagnostic_set(&diagnostic, DXIL_DIAG_STAGE_SEMA,
            DXIL_DIAG_UNKNOWN_IDENTIFIER, source, length, offsets[i]);
        CHECK(diagnostic.context_len == expected_len);
        CHECK(diagnostic.context[expected_len] == '\0');
        CHECK(memcmp(diagnostic.context, source + start, expected_len) == 0);
        CHECK(diagnostic.line == 1u);
        CHECK(diagnostic.column == offsets[i] + 1u);
        int written = snprintf(expected, sizeof(expected),
            "sema: UnknownIdentifier at 1:%zu near `%.*s`",
            offsets[i] + 1u, (int)expected_len, source + start);
        CHECK(written > 0 && (size_t)written < sizeof(expected));
        CHECK(strcmp(diagnostic.message, expected) == 0);
        CHECK(diagnostic.message_len == (size_t)written);
    }
}

int main(void) {
    check_lower();
    check_emit();
    check_diagnostic_units();
    check_diagnostic_context_boundary();
    TEST_SUMMARY("test_dxil_emit");
}
