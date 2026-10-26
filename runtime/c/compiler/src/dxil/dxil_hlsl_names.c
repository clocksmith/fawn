/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"
#include <stdio.h>
#include <string.h>

/* A caller-built module need not obey WGSL's reserved-identifier rules. Each
 * observed name occupies at most one salt, so one more slot than the bounded
 * number of names always supplies a namespace absent from the caller module. */
#define NAME_SLOTS (DXIL_IR_MAX_STRUCTS * (1u + DXIL_IR_MAX_STRUCT_FIELDS) + \
    DXIL_IR_MAX_GLOBALS + DXIL_IR_MAX_FUNCTIONS * (1u + DXIL_IR_MAX_PARAMS + \
    DXIL_IR_MAX_LOCALS + 2u * DXIL_IR_MAX_EXPRS) + 1u)
#define TEMPORARY_NAMESPACE "__doe_temporary_"
typedef struct NameSelection {
    bool conflict;
    bool used[NAME_SLOTS];
} NameSelection;

static void observe(NameSelection *selection, const char *name) {
    if (name == NULL) return;
    if (strncmp(name, "doe_store_", 10u) == 0 || strncmp(name, "doe_pointer_", 12u) == 0)
        selection->conflict = true;
    const size_t prefix_length = sizeof(TEMPORARY_NAMESPACE) - 1u;
    if (strncmp(name, TEMPORARY_NAMESPACE, prefix_length) != 0) return;
    name += prefix_length;
    if (*name < '0' || *name > '9') return;
    uint32_t salt = 0u;
    while (*name >= '0' && *name <= '9') {
        uint32_t digit = (uint32_t)(*name - '0');
        if (salt > (NAME_SLOTS - 1u - digit) / 10u) return;
        salt = salt * 10u + digit;
        ++name;
    }
    if (*name == '_') selection->used[salt] = true;
}

DxilStatus dxil_hlsl_choose_temporary_names(DxilHlslEmitter *emitter) {
    NameSelection selection = {0};
    const DxilIrModule *module = emitter->module;
    for (uint32_t i = 0u; i < module->struct_count; ++i) {
        observe(&selection, module->structs[i].name);
        for (uint32_t j = 0u; j < module->structs[i].field_count; ++j)
            observe(&selection, module->structs[i].fields[j].name);
    }
    for (uint32_t i = 0u; i < module->global_count; ++i) observe(&selection, module->globals[i].name);
    for (uint32_t i = 0u; i < module->function_count; ++i) {
        const DxilIrFunction *function = &module->functions[i];
        observe(&selection, function->name);
        for (uint32_t j = 0u; j < function->param_count; ++j) observe(&selection, function->params[j].name);
        for (uint32_t j = 0u; j < function->local_count; ++j) observe(&selection, function->locals[j].name);
        for (uint32_t j = 0u; j < function->expr_count; ++j) {
            if (function->exprs[j].kind == DXIL_IR_EXPR_CALL)
                observe(&selection, function->exprs[j].name);
            if (function->exprs[j].kind == DXIL_IR_EXPR_MEMBER)
                observe(&selection, function->exprs[j].field_name);
        }
    }
    emitter->temporary_prefix[0] = '\0';
    if (!selection.conflict) return DXIL_OK;
    for (uint32_t salt = 0u; salt < NAME_SLOTS; ++salt) {
        if (selection.used[salt]) continue;
        int size = snprintf(emitter->temporary_prefix, sizeof(emitter->temporary_prefix),
            TEMPORARY_NAMESPACE "%u_", salt);
        return size >= 0 && (size_t)size < sizeof(emitter->temporary_prefix) ? DXIL_OK : DXIL_OUTPUT_TOO_LARGE;
    }
    return DXIL_INVALID_IR;
}

DxilStatus dxil_hlsl_temporary_name(const DxilHlslEmitter *emitter, const char *kind,
    uint32_t owner, char buffer[DXIL_HLSL_TEMPORARY_NAME_CAP]) {
    int size = snprintf(buffer, DXIL_HLSL_TEMPORARY_NAME_CAP, "%s%s%u", emitter->temporary_prefix, kind, owner);
    return size >= 0 && (size_t)size < DXIL_HLSL_TEMPORARY_NAME_CAP ? DXIL_OK : DXIL_OUTPUT_TOO_LARGE;
}
DxilStatus dxil_hlsl_write_temporary(DxilHlslEmitter *emitter, const char *kind, uint32_t owner) {
    char name[DXIL_HLSL_TEMPORARY_NAME_CAP];
    DxilStatus status = dxil_hlsl_temporary_name(emitter, kind, owner, name);
    return status == DXIL_OK ? dxil_hlsl_write_text(emitter, name) : status;
}
