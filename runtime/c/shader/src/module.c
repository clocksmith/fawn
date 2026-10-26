/* SPDX-License-Identifier: MIT */
#include "internal.h"

static bool module_layout(const DoeCompilerValidationResult *result, size_t notices, size_t source, size_t identity,
    size_t *bytes, size_t offsets[6])
{
    *bytes = sizeof(doe_shader);
    return shader_reserve(bytes,source,1,1,&offsets[0]) &&
        shader_reserve(bytes,identity,1,1,&offsets[1]) &&
        shader_reserve(bytes,result->required_overrides,sizeof(DoeCompilerOverrideV13),
            alignof(DoeCompilerOverrideV13),&offsets[2]) &&
        shader_reserve(bytes,result->required_entries,sizeof(DoeCompilerModuleEntryV13),
            alignof(DoeCompilerModuleEntryV13),&offsets[3]) &&
        shader_reserve(bytes,result->required_name_bytes,1,1,&offsets[4]) &&
        shader_reserve(bytes,notices,sizeof(DoeCompilerNoticeV18),alignof(DoeCompilerNoticeV18),&offsets[5]);
}
doe_shader_status doe_shader_create_module(const doe_shader_options *options,
    size_t override_capacity, const char *source, size_t source_size,
    doe_shader **out, doe_shader_error *error)
{
    if (!shader_options_valid(options) || !out || (!source && source_size) ||
        options->selection.mode != DOE_COMPILER_SELECT_NONE || options->selection.reserved || options->selection.name || options->selection.name_length)
        return shader_fail(error,DOE_SHADER_INVALID_ARGUMENT,DOE_COMPILER_INPUT_ERROR,NULL);
    doe_shader_options saved = *options;
    DoeCompilerSpecializationCapacities capacities = {.target={.stages={.module=saved.capacities,
        .interface_attributes=saved.interface_attributes,.stage_interfaces=saved.stage_interfaces}},.overrides=override_capacity};
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status = doe_compiler_specialization_requirements(DOE_COMPILER_SPECIALIZATION_ABI_VERSION,
        (uint32_t)saved.target,&capacities,sizeof(capacities),&requirements,sizeof(requirements));
    if (status != DOE_COMPILER_OK) return shader_fail(error,DOE_SHADER_COMPILE_FAILED,status,NULL);
    shader_input input;
    doe_shader_status acquired = shader_input_allocate(&saved.allocator,&requirements,source,source_size,
        saved.compiler_identity,saved.compiler_identity_size,source_size <= saved.capacities.typed.source_bytes,NULL,0,&input);
    if (acquired != DOE_SHADER_OK) return shader_fail(error,acquired,DOE_COMPILER_OK,NULL);
    void *workspace = input.allocation;
    DoeCompilerDiagnosticResultV18 report = {0};
    DoeCompilerValidationResult *result = &report.validation;
    doe_shader *module = NULL;
    doe_shader_status outcome = DOE_SHADER_COMPILE_FAILED;
    for (;;) {
        status = doe_compiler_validate_module_diagnostics(DOE_COMPILER_DIAGNOSTICS_ABI_VERSION,&capacities,sizeof(capacities),
            workspace,requirements.size_bytes,input.source,source_size,saved.semantic_flags,NULL,0,&report,sizeof(report));
        if (status == DOE_COMPILER_OK) break;
        if (!shader_grow_pools(&saved,&capacities.target.stages,&capacities.overrides,
                status,result->exhausted_pool,result->minimum_pool_capacity)) goto finished;
        DoeCompilerWorkspaceRequirements next;
        status = doe_compiler_specialization_requirements(DOE_COMPILER_SPECIALIZATION_ABI_VERSION,
            (uint32_t)saved.target,&capacities,sizeof(capacities),&next,sizeof(next));
        if (status != DOE_COMPILER_OK) goto finished;
        outcome = shader_input_resize(&saved.allocator,&requirements,&next,source_size,
            saved.compiler_identity_size,source_size <= saved.capacities.typed.source_bytes,&input);
        if (outcome != DOE_SHADER_OK) goto finished;
        requirements = next;
        workspace = input.allocation;
        outcome = DOE_SHADER_COMPILE_FAILED;
    }
    if (status != DOE_COMPILER_OK) goto finished;
    size_t bytes, offsets[6];
    if (!result->required_is_exact || !module_layout(result,report.required_notices,source_size,saved.compiler_identity_size,&bytes,offsets)) {
        outcome = DOE_SHADER_OVERFLOW;
        goto finished;
    }
    module = saved.allocator.allocate(saved.allocator.userdata,bytes,alignof(max_align_t));
    if (!module) {
        outcome = DOE_SHADER_OUT_OF_MEMORY;
        goto finished;
    }
    memset(module,0,sizeof(*module));
    atomic_init(&module->references,1);
    module->kind = SHADER_MODULE;
    module->allocator = saved.allocator;
    module->allocation_size = bytes;
    module->source_size = source_size;
    module->identity_size = saved.compiler_identity_size;
    module->semantic_flags = saved.semantic_flags;
    module->override_capacity = capacities.overrides;
    module->source = (char *)module + offsets[0];
    module->identity = (unsigned char *)module + offsets[1];
    module->options = saved;
    module->options.compiler_identity = module->identity;
    module->options.selection = (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NONE};
    module->validation = (DoeCompilerValidationOutputs){
        .overrides=result->required_overrides ? (DoeCompilerOverrideV13 *)((unsigned char *)module+offsets[2]) : NULL,
        .override_capacity=result->required_overrides,.override_record_size=sizeof(DoeCompilerOverrideV13),
        .entries=result->required_entries ? (DoeCompilerModuleEntryV13 *)((unsigned char *)module+offsets[3]) : NULL,
        .entry_capacity=result->required_entries,.entry_record_size=sizeof(DoeCompilerModuleEntryV13),
        .names=result->required_name_bytes ? (char *)module+offsets[4] : NULL,.name_capacity=result->required_name_bytes};
    module->notices=report.required_notices ? (DoeCompilerNoticeV18 *)((unsigned char *)module+offsets[5]) : NULL;
    DoeCompilerDiagnosticOutputsV18 diagnostic_outputs={.metadata=module->validation,.notices=module->notices,
        .notice_capacity=report.required_notices,.notice_record_size=sizeof(DoeCompilerNoticeV18)};
    if (source_size) memcpy(module->source,input.source,source_size);
    memcpy(module->identity,input.identity,saved.compiler_identity_size);
    status = doe_compiler_validate_module_diagnostics(DOE_COMPILER_DIAGNOSTICS_ABI_VERSION,&capacities,sizeof(capacities),
        workspace,requirements.size_bytes,module->source,source_size,saved.semantic_flags,
        &diagnostic_outputs,sizeof(diagnostic_outputs),&report,sizeof(report));
    if (status == DOE_COMPILER_OK) {
        module->notice_count = report.published_notices;
        module->override_count = result->published_overrides;
        module->module_entry_count = result->published_entries;
        module->module_name_bytes = result->published_name_bytes;
        outcome = DOE_SHADER_OK;
    }
finished:
    saved.allocator.deallocate(saved.allocator.userdata,workspace,input.bytes,requirements.alignment_bytes);
    if (outcome == DOE_SHADER_OK) *out = module;
    else doe_shader_release(module);
    return shader_fail(error,outcome,status,&result->diagnostic);
}

doe_shader_status doe_shader_borrow_module(const doe_shader *module, doe_shader_module_view *out)
{
    if (!module || !out || module->kind != SHADER_MODULE) return DOE_SHADER_INVALID_ARGUMENT;
    *out = (doe_shader_module_view){.target=module->options.target,.semantic_flags=module->semantic_flags,
        .overrides=module->validation.overrides,.override_count=module->override_count,
        .entries=module->validation.entries,.entry_count=module->module_entry_count,
        .names=module->validation.names,.name_bytes=module->module_name_bytes};
    return DOE_SHADER_OK;
}

const DoeCompilerNoticeV18 *doe_shader_get_diagnostics(const doe_shader *shader, size_t *count)
{
    if (count) *count=0;
    if (!shader) return NULL;
    if (shader->kind==SHADER_SPECIALIZED) shader=shader->module;
    if (!shader || shader->kind!=SHADER_MODULE) return NULL;
    if (count) *count=shader->notice_count;
    return shader->notices;
}

bool doe_shader_module_matches(const doe_shader *module, doe_shader_target target,
    uint32_t semantic_flags, const char *source, size_t size,
    const void *identity, size_t identity_size)
{
    return module && module->kind == SHADER_MODULE && module->options.target == target &&
        module->semantic_flags == semantic_flags && (!size || source) && (!identity_size || identity) &&
        size == module->source_size && identity_size == module->identity_size &&
        (!size || !memcmp(source,module->source,size)) &&
        (!identity_size || !memcmp(identity,module->identity,identity_size));
}

size_t doe_shader_owned_bytes(const doe_shader *shader)
{
    return shader ? shader->allocation_size : 0;
}
