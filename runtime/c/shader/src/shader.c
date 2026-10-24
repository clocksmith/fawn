/* SPDX-License-Identifier: MIT
 * Owned compilation result derived from WGSL runtime TranslationInfo lifetime,
 * using the new compiler's actual typed metadata, not the retained Zig layout. */
#include "internal.h"

static DoeCompilerStatus text_requirements(doe_shader_target target,
    const DoeCompilerTextCapacities *capacities, DoeCompilerWorkspaceRequirements *requirements)
{
    if (target == DOE_SHADER_HLSL)
        return doe_compiler_hlsl_requirements(DOE_COMPILER_HLSL_ABI_VERSION,
            capacities, sizeof(*capacities), requirements, sizeof(*requirements));
    return doe_compiler_metal_requirements(DOE_COMPILER_METAL_ABI_VERSION,
        capacities, sizeof(*capacities), requirements, sizeof(*requirements));
}

static DoeCompilerStatus compile_text(doe_shader_target target,
    const DoeCompilerTextCapacities *capacities, void *workspace, size_t workspace_size,
    const char *source, size_t source_size, const DoeCompilerEntrySelectionV5 *selection,
    uint32_t flags, const DoeCompilerMetalOutputs *metal, const DoeCompilerHlslOutputs *hlsl,
    DoeCompilerTextResult *result)
{
    if (target == DOE_SHADER_HLSL)
        return doe_compiler_compile_hlsl(DOE_COMPILER_HLSL_ABI_VERSION,
            capacities, sizeof(*capacities), workspace, workspace_size, source, source_size,
            selection, sizeof(*selection), flags, hlsl, hlsl ? sizeof(*hlsl) : 0,
            result, sizeof(*result));
    return doe_compiler_compile_metal(DOE_COMPILER_METAL_ABI_VERSION,
        capacities, sizeof(*capacities), workspace, workspace_size, source, source_size,
        selection, sizeof(*selection), flags, metal, metal ? sizeof(*metal) : 0,
        result, sizeof(*result));
}

static DoeCompilerStatus specialization_requirements(const doe_shader *module,
    const DoeCompilerTextCapacities *target, DoeCompilerWorkspaceRequirements *requirements)
{
    const DoeCompilerSpecializationCapacities capacities = {*target,module->override_capacity};
    return doe_compiler_specialization_requirements(DOE_COMPILER_SPECIALIZATION_ABI_VERSION,
        (uint32_t)module->options.target,&capacities,sizeof(capacities),requirements,sizeof(*requirements));
}
static DoeCompilerStatus specialize(const doe_shader *module, const DoeCompilerTextCapacities *target,
    void *workspace, size_t workspace_size, const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t count, uint32_t flags,
    const DoeCompilerSpecializationOutputs *outputs, DoeCompilerTextResult *result)
{
    const DoeCompilerSpecializationCapacities capacities = {*target,module->override_capacity};
    return doe_compiler_specialize(DOE_COMPILER_SPECIALIZATION_ABI_VERSION,(uint32_t)module->options.target,
        &capacities,sizeof(capacities),workspace,workspace_size,module->source,module->source_size,
        selection,sizeof(*selection),values,count,sizeof(*values),flags,outputs,outputs ? sizeof(*outputs) : 0,
        result,sizeof(*result));
}

doe_shader_status doe_shader_create(const doe_shader_options *options,
    const char *source, size_t source_size, doe_shader **out, doe_shader_error *error)
{
    return shader_compile(options,source,source_size,NULL,NULL,0,out,error);
}

doe_shader_status shader_compile(const doe_shader_options *options,
    const char *source, size_t source_size, const doe_shader *module,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, doe_shader **out, doe_shader_error *error)
{
    if (!shader_options_valid(options) || !out || (!source && source_size))
        return shader_fail(error, DOE_SHADER_INVALID_ARGUMENT, DOE_COMPILER_INPUT_ERROR, NULL);
    doe_shader_options saved = *options;
    if (saved.selection.name_length > 63 ||
        (!saved.selection.name && saved.selection.name_length))
        return shader_fail(error, DOE_SHADER_INVALID_ARGUMENT, DOE_COMPILER_INPUT_ERROR, NULL);
    char selection_name[64] = {0};
    if (saved.selection.name_length) {
        memcpy(selection_name,saved.selection.name,saved.selection.name_length);
        saved.selection.name = selection_name;
    }
    const bool textual = saved.target == DOE_SHADER_METAL || saved.target == DOE_SHADER_HLSL;
    const DoeCompilerStageCapacities capacities = {.module = saved.capacities,
        .interface_attributes = saved.interface_attributes, .stage_interfaces = saved.stage_interfaces};
    DoeCompilerTextCapacities text_capacities = {.stages = capacities};
    DoeCompilerTextResult text_result = {0};
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status = module ? specialization_requirements(module,&text_capacities,&requirements) :
        textual ? text_requirements(saved.target, &text_capacities, &requirements) :
        doe_compiler_stage_requirements(DOE_COMPILER_ATOMIC_ABI_VERSION,
        &capacities, sizeof(capacities), &requirements, sizeof(requirements));
    if (status != DOE_COMPILER_OK)
        return shader_fail(error, DOE_SHADER_COMPILE_FAILED, status, NULL);
    /* Oversized source goes directly to the compiler's canonical capacity
     * rejection; never allocate/copy beyond the explicitly supplied source pool. */
    shader_input input;
    doe_shader_status acquired = shader_input_allocate(&saved.allocator,&requirements,source,source_size,
        saved.compiler_identity,saved.compiler_identity_size,
        module == NULL && source_size <= saved.capacities.typed.source_bytes,&input);
    if (acquired != DOE_SHADER_OK) return shader_fail(error,acquired,DOE_COMPILER_OK,NULL);
    void *workspace = input.allocation;
    size_t workspace_bytes = input.bytes;
    source = input.source;
    saved.compiler_identity = input.identity;
    DoeCompilerStageResult result = {0};
    if (module) {
        status = specialize(module,&text_capacities,workspace,requirements.size_bytes,&saved.selection,
            values,value_count,DOE_COMPILER_TEXT_CODE | DOE_COMPILER_TEXT_REFLECTION |
            DOE_COMPILER_TEXT_MEASURE | saved.semantic_flags,NULL,&text_result);
        result = text_result.stages;
    } else if (textual) {
        status = compile_text(saved.target, &text_capacities, workspace, requirements.size_bytes,
            source, source_size, &saved.selection, DOE_COMPILER_TEXT_CODE |
            DOE_COMPILER_TEXT_REFLECTION | DOE_COMPILER_TEXT_MEASURE | saved.semantic_flags, NULL, NULL, &text_result);
        result = text_result.stages;
    } else {
        status = doe_compiler_compile_stages(DOE_COMPILER_ATOMIC_ABI_VERSION,
            &capacities, sizeof(capacities), workspace, requirements.size_bytes,
            source, source_size, &saved.selection, sizeof(saved.selection),
            DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION | DOE_COMPILER_MODULE_MEASURE | saved.semantic_flags,
            NULL, 0, &result, sizeof(result));
    }
    doe_shader *shader = NULL;
    doe_shader_status outcome = DOE_SHADER_COMPILE_FAILED;
    if (status != DOE_COMPILER_OK) goto finished;
    size_t key_bytes = 0;
    for (size_t i = 0; i < value_count; ++i) {
        if (values[i].key_length > SIZE_MAX - key_bytes) {
            outcome = DOE_SHADER_OVERFLOW;
            goto finished;
        }
        key_bytes += values[i].key_length;
    }
    size_t bytes, offsets[15], value_offset = 0, key_offset = 0;
    if (!result.resources.module.required_is_exact || !shader_layout(saved.target, result.resources.module.required, result.resources.required_handles, result.required_interfaces, module ? 0 : source_size,
            module ? 0 : saved.compiler_identity_size, text_result.required_text_bytes,
            text_result.required_bindings, text_result.required_entries, &bytes, offsets) ||
        !shader_reserve(&bytes,value_count,sizeof(*values),alignof(DoeCompilerOverrideValueV13),&value_offset) ||
        !shader_reserve(&bytes,key_bytes,1,1,&key_offset)) {
        outcome = DOE_SHADER_OVERFLOW;
        goto finished;
    }
    shader = saved.allocator.allocate(saved.allocator.userdata, bytes, alignof(max_align_t));
    if (!shader) {
        outcome = DOE_SHADER_OUT_OF_MEMORY;
        goto finished;
    }
    memset(shader, 0, sizeof(*shader));
    atomic_init(&shader->references, 1);
    shader->allocator = saved.allocator;
    shader->kind = module ? SHADER_SPECIALIZED : SHADER_LEGACY;
    shader->allocation_size = bytes;
    shader->source_size = source_size;
    shader->identity_size = saved.compiler_identity_size;
    shader->selection_mode = saved.selection.mode;
    shader->semantic_flags = saved.semantic_flags;
    shader->selection_size = saved.selection.name_length;
    if (saved.selection.name_length)
        memcpy(shader->selection_name, saved.selection.name, saved.selection.name_length);
    shader_assign_outputs(shader, offsets, result.resources.module.required, result.resources.required_handles, result.required_interfaces);
    if (module) {
        outcome = doe_shader_retain((doe_shader *)module);
        if (outcome != DOE_SHADER_OK) goto finished;
        shader->module = (doe_shader *)module;
        shader->source = module->source;
        shader->identity = module->identity;
    } else {
        if (source_size) memcpy(shader->source,source,source_size);
        memcpy(shader->identity,saved.compiler_identity,saved.compiler_identity_size);
    }
    shader->value_count = value_count;
    shader->values = value_count ? (DoeCompilerOverrideValueV13 *)((unsigned char *)shader + value_offset) : NULL;
    char *keys = (char *)shader + key_offset;
    for (size_t i = 0; i < value_count; ++i) {
        memcpy(&shader->values[i],&values[i],sizeof(*values));
        shader->values[i].key = keys;
        if (values[i].key_length) memcpy(keys,values[i].key,values[i].key_length);
        keys += values[i].key_length;
    }
    outcome = DOE_SHADER_COMPILE_FAILED;
    saved.compiler_identity = shader->identity;
    if (textual) {
        saved.allocator.deallocate(saved.allocator.userdata, workspace,
            workspace_bytes, requirements.alignment_bytes);
        workspace = NULL;
        text_capacities.text_bytes = text_result.required_text_bytes;
        status = module ? specialization_requirements(module,&text_capacities,&requirements) :
            text_requirements(saved.target, &text_capacities, &requirements);
        if (status != DOE_COMPILER_OK) goto finished;
        workspace_bytes = requirements.size_bytes;
        workspace = saved.allocator.allocate(saved.allocator.userdata,
            workspace_bytes, requirements.alignment_bytes);
        if (!workspace) {
            outcome = DOE_SHADER_OUT_OF_MEMORY;
            goto finished;
        }
    }
    DoeCompilerMetalOutputs text_outputs = {0};
    DoeCompilerHlslOutputs hlsl_outputs = {0};
    char *target_text = NULL;
    if (textual) {
        shader->outputs.resources.module.words = NULL;
        unsigned char *base = (unsigned char *)shader;
        target_text = text_result.required_text_bytes ? (char *)(base + offsets[12]) : NULL;
        if (saved.target == DOE_SHADER_HLSL) hlsl_outputs = (DoeCompilerHlslOutputs) {
            .stages = shader->outputs, .text = target_text,
            .text_capacity = text_result.required_text_bytes,
            .bindings = text_result.required_bindings ? (DoeCompilerHlslBindingV11 *)(base + offsets[13]) : NULL,
            .binding_capacity = text_result.required_bindings,
            .binding_record_size = sizeof(DoeCompilerHlslBindingV11),
            .entries = text_result.required_entries ? (DoeCompilerHlslEntryV11 *)(base + offsets[14]) : NULL,
            .entry_capacity = text_result.required_entries,
            .entry_record_size = sizeof(DoeCompilerHlslEntryV11)
        };
        else text_outputs = (DoeCompilerMetalOutputs) {
            .stages = shader->outputs,
            .text = target_text,
            .text_capacity = text_result.required_text_bytes,
            .bindings = text_result.required_bindings ? (DoeCompilerMetalBindingV12 *)(base + offsets[13]) : NULL,
            .binding_capacity = text_result.required_bindings,
            .binding_record_size = sizeof(DoeCompilerMetalBindingV12),
            .entries = text_result.required_entries ? (DoeCompilerMetalEntryV12 *)(base + offsets[14]) : NULL,
            .entry_capacity = text_result.required_entries,
            .entry_record_size = sizeof(DoeCompilerMetalEntryV12)
        };
        if (module) {
            DoeCompilerSpecializationOutputs outputs = {0};
            if (saved.target == DOE_SHADER_METAL) outputs.metal = text_outputs;
            else outputs.hlsl = hlsl_outputs;
            status = specialize(module,&text_capacities,workspace,requirements.size_bytes,&saved.selection,
                shader->values,value_count,DOE_COMPILER_TEXT_CODE | DOE_COMPILER_TEXT_REFLECTION | saved.semantic_flags,
                &outputs,&text_result);
        } else status = compile_text(saved.target, &text_capacities, workspace, requirements.size_bytes,
            shader->source, source_size, &saved.selection, DOE_COMPILER_TEXT_CODE |
            DOE_COMPILER_TEXT_REFLECTION | saved.semantic_flags, &text_outputs, &hlsl_outputs, &text_result);
        result = text_result.stages;
    } else if (module) {
        const DoeCompilerSpecializationOutputs outputs = {.spirv=shader->outputs};
        status = specialize(module,&text_capacities,workspace,requirements.size_bytes,&saved.selection,
            shader->values,value_count,DOE_COMPILER_TEXT_CODE | DOE_COMPILER_TEXT_REFLECTION | saved.semantic_flags,
            &outputs,&text_result);
        result = text_result.stages;
    } else {
        status = doe_compiler_compile_stages(DOE_COMPILER_ATOMIC_ABI_VERSION,
            &capacities, sizeof(capacities), workspace, requirements.size_bytes,
            shader->source, source_size, &saved.selection, sizeof(saved.selection),
            DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION | saved.semantic_flags,
            &shader->outputs, sizeof(shader->outputs), &result, sizeof(result));
    }
    if (status == DOE_COMPILER_OK) {
        shader->view = (doe_shader_view) {
            .target = saved.target, .target_language_version = text_result.language_version,
            .text = target_text, .text_size = text_result.published_text_bytes,
            .metal_bindings = text_outputs.bindings,
            .metal_binding_count = saved.target == DOE_SHADER_METAL ? text_result.published_bindings : 0,
            .metal_entries = text_outputs.entries,
            .metal_entry_count = saved.target == DOE_SHADER_METAL ? text_result.published_entries : 0,
            .hlsl_bindings = hlsl_outputs.bindings,
            .hlsl_binding_count = saved.target == DOE_SHADER_HLSL ? text_result.published_bindings : 0,
            .hlsl_entries = hlsl_outputs.entries,
            .hlsl_entry_count = saved.target == DOE_SHADER_HLSL ? text_result.published_entries : 0,
            .module = result.resources.module.module, .counts = result.resources.module.published,
            .words = shader->outputs.resources.module.words, .types = shader->outputs.resources.module.types,
            .members = shader->outputs.resources.module.members, .resources = shader->outputs.resources.module.resources,
            .workgroups = shader->outputs.resources.module.workgroups, .entries = shader->outputs.resources.module.entries,
            .entry_references = shader->outputs.resources.module.entry_references, .names = shader->outputs.resources.module.names,
            .handles = shader->outputs.resources.handles, .handle_count = result.resources.published_handles,
            .interfaces = shader->outputs.interfaces, .interface_count = result.published_interfaces
        };
        outcome = DOE_SHADER_OK;
    }
finished:
    if (workspace) saved.allocator.deallocate(saved.allocator.userdata, workspace,
        workspace_bytes, requirements.alignment_bytes);
    if (outcome != DOE_SHADER_OK && shader) doe_shader_release(shader);
    if (outcome == DOE_SHADER_OK) *out = shader;
    return shader_fail(error, outcome, status, &result.resources.module.diagnostic);
}

doe_shader_status doe_shader_retain(doe_shader *shader)
{
    if (!shader) return DOE_SHADER_INVALID_ARGUMENT;
    unsigned count = atomic_load_explicit(&shader->references, memory_order_relaxed);
    do {
        if (!count || count == UINT_MAX) return DOE_SHADER_OVERFLOW;
    } while (!atomic_compare_exchange_weak_explicit(&shader->references, &count,
        count + 1, memory_order_relaxed, memory_order_relaxed));
    return DOE_SHADER_OK;
}

void doe_shader_release(doe_shader *shader)
{
    if (!shader) return;
    unsigned previous = atomic_fetch_sub_explicit(&shader->references, 1, memory_order_acq_rel);
    if (!previous) abort();
    if (previous != 1) return;
    doe_shader_allocator allocator = shader->allocator;
    doe_shader *module = shader->module;
    allocator.deallocate(allocator.userdata,shader,shader->allocation_size,alignof(max_align_t));
    doe_shader_release(module);
}

doe_shader_status doe_shader_borrow(const doe_shader *shader, doe_shader_view *out)
{
    if (!shader || !out || shader->kind == SHADER_MODULE) return DOE_SHADER_INVALID_ARGUMENT;
    *out = shader->view;
    return DOE_SHADER_OK;
}

bool doe_shader_matches(const doe_shader *shader, doe_shader_target target, uint32_t semantic_flags,
    const char *source, size_t size,
    const void *identity, size_t identity_size, const DoeCompilerEntrySelectionV5 *selection)
{
    if (!shader || shader->kind != SHADER_LEGACY || shader->view.target != target || shader->semantic_flags != semantic_flags || !selection || (!source && size) || (!identity && identity_size) ||
        (!selection->name && selection->name_length) || selection->reserved ||
        size != shader->source_size || identity_size != shader->identity_size ||
        selection->mode != shader->selection_mode || selection->name_length != shader->selection_size)
        return false;
    return (!size || !memcmp(source, shader->source, size)) &&
        (!identity_size || !memcmp(identity, shader->identity, identity_size)) &&
        (!selection->name_length || !memcmp(selection->name, shader->selection_name, selection->name_length));
}
