/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source.h"
#include "../../include/doe_compiler_specialization.h"
#include "../../include/doe_compiler_pool_growth.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define DXIL_SOURCE_LIMIT (2u * 1024u * 1024u)
#define DXIL_WORKSPACE_LIMIT (256u * 1024u * 1024u)

typedef struct SourceStorage {
    void *workspace;
    DoeCompilerValidationOutputs validation;
    DoeCompilerSpecializationOutputs output;
} SourceStorage;

static DoeCompilerSpecializationCapacities initial_capacities(void) {
    const DoeCompilerTypedCapacities typed = {
        .source_bytes = DXIL_SOURCE_LIMIT, .tokens = 2048u, .statements = 256u,
        .expressions = 1024u, .types = 128u, .members = 256u,
        .resources = 32u, .access_steps = 2048u, .constants = 1024u,
        .constant_edges = 2048u, .ir_instructions = 8192u,
        .ir_operands = 4096u, .target_declarations = 4096u,
        .traversal_items = 256u, .words = 0u,
        .expression_depth = 32u, .control_depth = 16u, .type_syntax_depth = 32u
    };
    return (DoeCompilerSpecializationCapacities){.target = {.stages = {
        .module = {.typed = typed, .entries = 64u, .entry_inputs = 256u,
            .entry_references = 8192u},
        .interface_attributes = 256u, .stage_interfaces = 256u}},
        .overrides = 256u};
}

static bool grow(DoeCompilerSpecializationCapacities *caps,
    DoeCompilerStatus status, uint32_t pool, size_t minimum) {
    if (pool == DOE_COMPILER_POOL_OVERRIDES)
        return (status == DOE_COMPILER_INPUT_ERROR || status == DOE_COMPILER_OUTPUT_TOO_SMALL) &&
            doe_compiler_grow_pool_capacity(&caps->overrides, minimum);
    return doe_compiler_grow_stage_pool(&caps->target.stages, status, pool, minimum);
}

static void release(SourceStorage *storage) {
    DoeCompilerHlslOutputs *h = &storage->output.hlsl;
    DoeCompilerModuleOutputs *m = &h->stages.resources.module;
    free(storage->workspace);
    free(storage->validation.overrides);
    free(storage->validation.entries);
    free(storage->validation.names);
    free(m->types); free(m->members); free(m->resources); free(m->workgroups);
    free(m->entries); free(m->entry_references); free(m->names);
    free(h->stages.resources.handles); free(h->stages.interfaces);
    free(h->bindings); free(h->entries); free(h->text);
}

static bool set_error(DxilDiagnostic *diagnostic, DxilDiagKind *out_error,
    DxilDiagnosticStage stage, DxilDiagKind kind, const char *detail) {
    dxil_diagnostic_set_detail(diagnostic, stage, kind, detail);
    if (out_error != NULL) *out_error = kind;
    return false;
}

static bool compiler_error(const char *source, size_t length,
    const DoeCompilerDiagnostic *error, uint32_t pool, DxilDiagnostic *diagnostic,
    DxilDiagKind *out_error) {
    DxilDiagKind kind = DXIL_DIAG_INVALID_WGSL;
    DxilDiagnosticStage stage = DXIL_DIAG_STAGE_SEMA;
    switch (error->status) {
        case DOE_COMPILER_OK: case DOE_COMPILER_ABI_MISMATCH:
            kind = DXIL_DIAG_INVALID_IR;
            break;
        case DOE_COMPILER_INPUT_ERROR:
            if (pool != DOE_COMPILER_POOL_NONE) kind = DXIL_DIAG_OUTPUT_TOO_LARGE;
            break;
        case DOE_COMPILER_PARSE_ERROR:
            kind = DXIL_DIAG_UNEXPECTED_TOKEN;
            stage = DXIL_DIAG_STAGE_PARSER;
            break;
        case DOE_COMPILER_UNSUPPORTED:
            kind = DXIL_DIAG_UNSUPPORTED_CONSTRUCT;
            break;
        case DOE_COMPILER_SEMANTIC_ERROR:
            break;
        case DOE_COMPILER_OUTPUT_TOO_SMALL:
            kind = DXIL_DIAG_OUTPUT_TOO_LARGE;
            break;
    }
    DxilDiagnostic location = {0};
    dxil_diagnostic_set(&location, stage, kind, source, length, error->offset);
    /* Keep the canonical first semantic message and original source context;
     * do not infer a narrower error kind by parsing diagnostic strings. */
    dxil_diagnostic_set_detail(diagnostic, stage, kind, error->message);
    if (diagnostic != NULL) {
        diagnostic->line = error->line;
        diagnostic->column = error->column;
        diagnostic->context_len = location.context_len;
        memcpy(diagnostic->context, location.context, sizeof(diagnostic->context));
    }
    if (out_error != NULL) *out_error = kind;
    return false;
}

static bool workspace(SourceStorage *storage,
    const DoeCompilerSpecializationCapacities *caps, uint32_t target,
    size_t *bytes, DxilDiagnostic *diagnostic, DxilDiagKind *out_error) {
    DoeCompilerWorkspaceRequirements required;
    if (doe_compiler_specialization_requirements(DOE_COMPILER_SPECIALIZATION_ABI_VERSION,
            target, caps, sizeof(*caps), &required, sizeof(required)) != DOE_COMPILER_OK ||
        required.size_bytes > DXIL_WORKSPACE_LIMIT)
        return set_error(diagnostic, out_error, DXIL_DIAG_STAGE_IR_BUILDER,
            DXIL_DIAG_OUTPUT_TOO_LARGE, "DXIL host compiler workspace exceeds its checked budget");
    free(storage->workspace);
    storage->workspace = malloc(required.size_bytes);
    if (storage->workspace == NULL)
        return set_error(diagnostic, out_error, DXIL_DIAG_STAGE_IR_BUILDER,
            DXIL_DIAG_OUT_OF_MEMORY, "DXIL host compiler workspace allocation failed");
    *bytes = required.size_bytes;
    return true;
}

static bool validation_storage(SourceStorage *s, const DoeCompilerValidationResult *r) {
    s->validation = (DoeCompilerValidationOutputs){
        .override_capacity = r->required_overrides,
        .override_record_size = sizeof(DoeCompilerOverrideV13),
        .entry_capacity = r->required_entries,
        .entry_record_size = sizeof(DoeCompilerModuleEntryV13),
        .name_capacity = r->required_name_bytes};
    if (r->required_overrides != 0u)
        s->validation.overrides = calloc(r->required_overrides, sizeof(*s->validation.overrides));
    if (r->required_entries != 0u)
        s->validation.entries = calloc(r->required_entries, sizeof(*s->validation.entries));
    if (r->required_name_bytes != 0u)
        s->validation.names = malloc(r->required_name_bytes);
    return (r->required_overrides == 0u || s->validation.overrides != NULL) &&
        (r->required_entries == 0u || s->validation.entries != NULL) &&
        (r->required_name_bytes == 0u || s->validation.names != NULL);
}

static bool output_storage(SourceStorage *s, const DoeCompilerTextResult *r) {
    DoeCompilerHlslOutputs *h = &s->output.hlsl;
    DoeCompilerModuleOutputs *m = &h->stages.resources.module;
    const DoeCompilerModuleOutputCounts *n = &r->stages.resources.module.required;
    *m = (DoeCompilerModuleOutputs){
        .type_capacity = n->types, .type_record_size = sizeof(DoeCompilerTypeV4),
        .member_capacity = n->members, .member_record_size = sizeof(DoeCompilerMemberV4),
        .resource_capacity = n->resources, .resource_record_size = sizeof(DoeCompilerResourceV4),
        .workgroup_capacity = n->workgroups, .workgroup_record_size = sizeof(DoeCompilerWorkgroupV5),
        .entry_capacity = n->entries, .entry_record_size = sizeof(DoeCompilerEntryV5),
        .entry_reference_capacity = n->entry_references, .name_capacity = n->name_bytes};
    h->stages.resources.handle_capacity = r->stages.resources.required_handles;
    h->stages.resources.handle_record_size = sizeof(DoeCompilerHandleV6);
    h->stages.interface_capacity = r->stages.required_interfaces;
    h->stages.interface_record_size = sizeof(DoeCompilerStageInterfaceV7);
    h->text_capacity = r->required_text_bytes;
    h->binding_capacity = r->required_bindings;
    h->binding_record_size = sizeof(DoeCompilerHlslBindingV11);
    h->entry_capacity = r->required_entries;
    h->entry_record_size = sizeof(DoeCompilerHlslEntryV11);
    if (n->types != 0u) m->types = calloc(n->types, sizeof(*m->types));
    if (n->members != 0u) m->members = calloc(n->members, sizeof(*m->members));
    if (n->resources != 0u) m->resources = calloc(n->resources, sizeof(*m->resources));
    if (n->workgroups != 0u) m->workgroups = calloc(n->workgroups, sizeof(*m->workgroups));
    if (n->entries != 0u) m->entries = calloc(n->entries, sizeof(*m->entries));
    if (n->entry_references != 0u) m->entry_references = calloc(n->entry_references, sizeof(*m->entry_references));
    if (n->name_bytes != 0u) m->names = malloc(n->name_bytes);
    if (h->stages.resources.handle_capacity != 0u)
        h->stages.resources.handles = calloc(h->stages.resources.handle_capacity, sizeof(*h->stages.resources.handles));
    if (h->stages.interface_capacity != 0u)
        h->stages.interfaces = calloc(h->stages.interface_capacity, sizeof(*h->stages.interfaces));
    if (h->binding_capacity != 0u) h->bindings = calloc(h->binding_capacity, sizeof(*h->bindings));
    if (h->entry_capacity != 0u) h->entries = calloc(h->entry_capacity, sizeof(*h->entries));
    if (h->text_capacity != 0u) h->text = malloc(h->text_capacity);
    return (n->types == 0u || m->types != NULL) && (n->members == 0u || m->members != NULL) &&
        (n->resources == 0u || m->resources != NULL) && (n->workgroups == 0u || m->workgroups != NULL) &&
        (n->entries == 0u || m->entries != NULL) && (n->entry_references == 0u || m->entry_references != NULL) &&
        (n->name_bytes == 0u || m->names != NULL) &&
        (h->stages.resources.handle_capacity == 0u || h->stages.resources.handles != NULL) &&
        (h->stages.interface_capacity == 0u || h->stages.interfaces != NULL) &&
        (h->binding_capacity == 0u || h->bindings != NULL) &&
        (h->entry_capacity == 0u || h->entries != NULL) &&
        (h->text_capacity == 0u || h->text != NULL);
}

bool dxil_source_hlsl(const char *source, size_t length, DxilSourceHlsl *out,
    DxilDiagnostic *diagnostic, DxilDiagKind *out_error) {
    const int saved_errno = errno;
    SourceStorage storage = {0};
    DoeCompilerSpecializationCapacities caps = initial_capacities();
    DoeCompilerValidationResult validation = {0};
    DoeCompilerTextResult compiled = {0};
    size_t bytes = 0u;
    bool success = false;
    dxil_diagnostic_clear(diagnostic);
    if (out == NULL) {
        const bool failed = set_error(diagnostic, out_error, DXIL_DIAG_STAGE_PARSER,
            DXIL_DIAG_INVALID_WGSL, "DXIL source output is required");
        errno = saved_errno;
        return failed;
    }
    memset(out, 0, sizeof(*out));
    if ((source == NULL && length != 0u) || length > DXIL_SOURCE_LIMIT) {
        (void)set_error(diagnostic, out_error, DXIL_DIAG_STAGE_PARSER,
            DXIL_DIAG_INVALID_WGSL, "invalid or oversized DXIL source span");
        goto finish;
    }
    for (;;) {
        if (!workspace(&storage, &caps, DOE_COMPILER_TARGET_SPIRV, &bytes, diagnostic, out_error)) goto finish;
        const DoeCompilerStatus status = doe_compiler_validate_module(
            DOE_COMPILER_SPECIALIZATION_ABI_VERSION, &caps, sizeof(caps), storage.workspace,
            bytes, source, length, 0u, NULL, 0u, &validation, sizeof(validation));
        if (status == DOE_COMPILER_OK) break;
        if (!grow(&caps, status, validation.exhausted_pool, validation.minimum_pool_capacity)) {
            (void)compiler_error(source, length, &validation.diagnostic,
                validation.exhausted_pool, diagnostic, out_error);
            goto finish;
        }
    }
    if (validation.required_entries == 0u) {
        (void)set_error(diagnostic, out_error, DXIL_DIAG_STAGE_DXIL_EMIT,
            DXIL_DIAG_INVALID_IR, "DXIL source has no shader entry point");
        goto finish;
    }
    if (!validation_storage(&storage, &validation)) goto allocation_failure;
    if (doe_compiler_validate_module(DOE_COMPILER_SPECIALIZATION_ABI_VERSION,
            &caps, sizeof(caps), storage.workspace, bytes, source, length, 0u,
            &storage.validation, sizeof(storage.validation), &validation, sizeof(validation)) != DOE_COMPILER_OK) {
        (void)compiler_error(source, length, &validation.diagnostic,
            validation.exhausted_pool, diagnostic, out_error);
        goto finish;
    }
    const DoeCompilerNameV4 name = storage.validation.entries[0].name;
    const DoeCompilerEntrySelectionV5 selected = {.mode = DOE_COMPILER_SELECT_NAME,
        .name = storage.validation.names + name.offset, .name_length = name.length};
    for (;;) {
        if (!workspace(&storage, &caps, DOE_COMPILER_TARGET_HLSL, &bytes, diagnostic, out_error)) goto finish;
        const DoeCompilerStatus status = doe_compiler_specialize(
            DOE_COMPILER_SPECIALIZATION_ABI_VERSION, DOE_COMPILER_TARGET_HLSL,
            &caps, sizeof(caps), storage.workspace, bytes, source, length,
            &selected, sizeof(selected), NULL, 0u, sizeof(DoeCompilerOverrideValueV13),
            DOE_COMPILER_TEXT_CODE | DOE_COMPILER_TEXT_REFLECTION | DOE_COMPILER_TEXT_MEASURE,
            NULL, 0u, &compiled, sizeof(compiled));
        if (status == DOE_COMPILER_OK) break;
        if (!grow(&caps, status, compiled.stages.resources.module.exhausted_pool,
                compiled.stages.resources.module.minimum_pool_capacity)) {
            (void)compiler_error(source, length, &compiled.stages.resources.module.diagnostic,
                compiled.stages.resources.module.exhausted_pool, diagnostic, out_error);
            goto finish;
        }
    }
    caps.target.text_bytes = compiled.required_text_bytes;
    if (!workspace(&storage, &caps, DOE_COMPILER_TARGET_HLSL, &bytes, diagnostic, out_error)) goto finish;
    if (!output_storage(&storage, &compiled)) goto allocation_failure;
    if (doe_compiler_specialize(DOE_COMPILER_SPECIALIZATION_ABI_VERSION, DOE_COMPILER_TARGET_HLSL,
            &caps, sizeof(caps), storage.workspace, bytes, source, length,
            &selected, sizeof(selected), NULL, 0u, sizeof(DoeCompilerOverrideValueV13),
            DOE_COMPILER_TEXT_CODE | DOE_COMPILER_TEXT_REFLECTION,
            &storage.output, sizeof(storage.output), &compiled, sizeof(compiled)) != DOE_COMPILER_OK) {
        (void)compiler_error(source, length, &compiled.stages.resources.module.diagnostic,
            compiled.stages.resources.module.exhausted_pool, diagnostic, out_error);
        goto finish;
    }
    const DoeCompilerHlslEntryV11 *entry = &storage.output.hlsl.entries[0];
    const DoeCompilerModuleOutputs *module = &storage.output.hlsl.stages.resources.module;
    if (compiled.published_entries != 1u || entry->entry_index >= module->entry_capacity) {
        (void)set_error(diagnostic, out_error, DXIL_DIAG_STAGE_HLSL_EMIT,
            DXIL_DIAG_INVALID_IR, "HLSL entry publication does not match its selected source entry");
        goto finish;
    }
    switch (entry->stage) {
        case DOE_COMPILER_STAGE_COMPUTE: out->entry.stage = DXIL_STAGE_COMPUTE; break;
        case DOE_COMPILER_STAGE_VERTEX: out->entry.stage = DXIL_STAGE_VERTEX; break;
        case DOE_COMPILER_STAGE_FRAGMENT: out->entry.stage = DXIL_STAGE_FRAGMENT; break;
        default:
            (void)set_error(diagnostic, out_error, DXIL_DIAG_STAGE_HLSL_EMIT,
                DXIL_DIAG_INVALID_IR, "unknown stage in typed HLSL entry metadata");
            goto finish;
    }
    memcpy(out->entry_name, entry->name, sizeof(out->entry_name));
    out->entry.name = out->entry_name;
    memcpy(out->entry.workgroup_size, module->entries[entry->entry_index].workgroup_size,
        sizeof(out->entry.workgroup_size));
    out->entry.shader_model_major = 6u;
    out->entry.native16 = (entry->flags & DOE_COMPILER_HLSL_ENTRY_NATIVE16) != 0u;
    out->entry.shader_model_minor = out->entry.native16 ? 2u : 0u;
    out->text = storage.output.hlsl.text;
    out->text_length = compiled.published_text_bytes;
    storage.output.hlsl.text = NULL;
    success = true;
    goto finish;
allocation_failure:
    (void)set_error(diagnostic, out_error, DXIL_DIAG_STAGE_IR_BUILDER,
        DXIL_DIAG_OUT_OF_MEMORY, "DXIL host metadata allocation failed");
finish:
    release(&storage);
    if (!success) memset(out, 0, sizeof(*out));
    errno = saved_errno;
    return success;
}

void dxil_source_hlsl_free(DxilSourceHlsl *result) {
    if (result == NULL) return;
    free(result->text);
    memset(result, 0, sizeof(*result));
}
