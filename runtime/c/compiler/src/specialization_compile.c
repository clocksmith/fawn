/* SPDX-License-Identifier: MIT */
#include "override.h"
#include "compile_internal.h"
#include <errno.h>
#include <string.h>

static bool output_records(uint32_t target, const DoeCompilerSpecializationOutputs *outputs) {
    if (outputs == NULL) return true;
    const DoeCompilerStageOutputs *stages = target == DOE_COMPILER_TARGET_METAL ? &outputs->metal.stages :
        target == DOE_COMPILER_TARGET_HLSL ? &outputs->hlsl.stages : &outputs->spirv;
    if (!compiler_module_records(&stages->resources.module) ||
        stages->resources.handle_record_size != sizeof(DoeCompilerHandleV6) ||
        stages->interface_record_size != sizeof(DoeCompilerStageInterfaceV7)) return false;
    if (target == DOE_COMPILER_TARGET_METAL)
        return outputs->metal.binding_record_size == sizeof(DoeCompilerMetalBindingV12) &&
            outputs->metal.entry_record_size == sizeof(DoeCompilerMetalEntryV12);
    if (target == DOE_COMPILER_TARGET_HLSL)
        return outputs->hlsl.binding_record_size == sizeof(DoeCompilerHlslBindingV11) &&
            outputs->hlsl.entry_record_size == sizeof(DoeCompilerHlslEntryV11);
    return true;
}
static DoeCompilerStatus specialize(bool all_entries, bool metal_pipeline, uint32_t sample_mask,
    uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size, SamplingContext *sampling) {
    const int saved_errno = errno;
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != (metal_pipeline ? DOE_COMPILER_METAL_PIPELINE_ABI_VERSION :
        all_entries ? DOE_COMPILER_MODULE_SPECIALIZATION_ABI_VERSION : DOE_COMPILER_SPECIALIZATION_ABI_VERSION) ||
        capacities_size != sizeof(*caps) ||
        selection_size != sizeof(*selection) || value_record_size != sizeof(*values) ||
        result_size != sizeof(*result) || (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u) ||
        !output_records(target, outputs)) return DOE_COMPILER_ABI_MISMATCH;
    memset(result, 0, sizeof(*result));
    DoeCompilerModuleResult *module = &result->stages.resources.module;
    Source s = {.source=source, .length=length, .diagnostic=&module->diagnostic};
    DoeCompilerWorkspaceRequirements requirements;
    size_t base;
    bool valid = specialization_layout(target, caps, &base, &requirements) &&
        workspace != NULL && workspace_size >= requirements.size_bytes &&
        (uintptr_t)workspace % requirements.alignment_bytes == 0u &&
        selection != NULL && (all_entries || selection->mode != DOE_COMPILER_SELECT_NONE) && value_count < UINT32_MAX &&
        compiler_valid_span(values, value_count, sizeof(*values), _Alignof(DoeCompilerOverrideValueV13), true);
    if (valid) for (size_t i = 0u; i < value_count; ++i)
        if (values[i].key == NULL || values[i].key_length == 0u || values[i].key_length > length) valid = false;
    if (!valid) {
        (void)source_error(&s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid specialization capacities/workspace/selection/value spans");
        errno = saved_errno;
        return module->diagnostic.status;
    }
    SpecializationContext context = {.sampling=sampling, .values=values, .value_count=value_count,
        .capacities=caps, .selection=selection, .all_entries=all_entries,
        .metal_pipeline=metal_pipeline, .metal_sample_mask=sample_mask};
    specialization_bind(&context, workspace, base, caps);
    DoeCompilerStatus status;
    if (target == DOE_COMPILER_TARGET_SPIRV) {
        const DoeCompilerStageOutputs *out = outputs != NULL ? &outputs->spirv : NULL;
        status = compiler_compile_module(&caps->target.stages.module, workspace, workspace_size,
            source, length, selection, flags, out != NULL ? &out->resources.module : NULL, module,
            out != NULL ? &out->resources : NULL, &result->stages.resources, &caps->target.stages,
            out, &result->stages, DOE_COMPILER_ATOMIC_ABI_VERSION, &context);
    } else status = compiler_specialized_text(target, &caps->target, workspace, workspace_size,
        source, length, selection, flags, outputs, result, &context);
    errno = saved_errno;
    return status;
}

DoeCompilerStatus doe_compiler_specialize(uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    return specialize(false, false, UINT32_MAX, version, target, caps, capacities_size, workspace, workspace_size,
        source, length, selection, selection_size, values, value_count, value_record_size,
        flags, outputs, outputs_size, result, result_size, NULL);
}
DoeCompilerStatus doe_compiler_specialize_module(uint32_t version, uint32_t target,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    const DoeCompilerEntrySelectionV5 selection = {.mode = DOE_COMPILER_SELECT_NONE};
    return specialize(true, false, UINT32_MAX, version, target, caps, capacities_size, workspace, workspace_size,
        source, length, &selection, sizeof(selection), values, value_count, value_record_size,
        flags, outputs, outputs_size, result, result_size, NULL);
}

DoeCompilerStatus doe_compiler_specialize_metal_pipeline(uint32_t version,
    const DoeCompilerMetalPipelineOptionsV15 *options, size_t options_size,
    const DoeCompilerSpecializationCapacities *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    if (options_size != sizeof(*options)) return DOE_COMPILER_ABI_MISMATCH;
    if (options == NULL) return DOE_COMPILER_INPUT_ERROR;
    const uint32_t sample_mask = options->sample_mask;
    return specialize(false, true, sample_mask, version, DOE_COMPILER_TARGET_METAL,
        caps, capacities_size, workspace, workspace_size, source, length, selection, selection_size,
        values, value_count, value_record_size, flags, outputs, outputs_size, result, result_size, NULL);
}

static DoeCompilerStatus sampling_compile(uint32_t version, uint32_t target,
    const DoeCompilerSamplingOptionsV16 *options, size_t options_size,
    const DoeCompilerSamplingCapacitiesV16 *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSamplingOutputsV16 *outputs, size_t outputs_size,
    DoeCompilerSamplingResultV16 *result, size_t result_size, TranslationContext *translation) {
    const int saved_errno = errno;
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_SAMPLING_ABI_VERSION || options_size != sizeof(*options) ||
        capacities_size != sizeof(*caps) || result_size != sizeof(*result) ||
        selection_size != sizeof(*selection) || value_record_size != sizeof(*values) ||
        (outputs != NULL ? outputs_size != sizeof(*outputs) ||
            outputs->pair_record_size != sizeof(DoeCompilerSamplingPairV16) : outputs_size != 0u) ||
        !output_records(target, outputs != NULL ? &outputs->target : NULL)) return DOE_COMPILER_ABI_MISMATCH;
    memset(result, 0, sizeof(*result));
    DoeCompilerWorkspaceRequirements requirements;
    size_t base;
    if (options == NULL || (options->flags & ~DOE_COMPILER_SAMPLING_METAL_PIPELINE) != 0u ||
        (options->flags != 0u && target != DOE_COMPILER_TARGET_METAL) ||
        (options->flags == 0u && options->metal_sample_mask != UINT32_MAX) ||
        (flags & DOE_COMPILER_MODULE_REFLECTION) == 0u ||
        !sampling_layout(target, caps, &base, &requirements) || workspace == NULL ||
        workspace_size < requirements.size_bytes || (uintptr_t)workspace % requirements.alignment_bytes != 0u ||
        (outputs != NULL && !compiler_valid_span(outputs->pairs, outputs->pair_capacity,
            sizeof(*outputs->pairs), _Alignof(DoeCompilerSamplingPairV16), true))) {
        Source context = {.source=source, .length=length,
            .diagnostic=&result->target.stages.resources.module.diagnostic};
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "invalid sampling specialization options, flags or spans");
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    SamplingContext sampling = {.staging=(DoeCompilerSamplingPairV16 *)((unsigned char *)workspace + base),
        .capacity=caps->sampling_pairs, .outputs=outputs, .translation=translation};
    const bool metal = (options->flags & DOE_COMPILER_SAMPLING_METAL_PIPELINE) != 0u;
    const DoeCompilerStatus status = specialize(false, metal, options->metal_sample_mask,
        metal ? DOE_COMPILER_METAL_PIPELINE_ABI_VERSION : DOE_COMPILER_SPECIALIZATION_ABI_VERSION,
        target, &caps->specialization, sizeof(caps->specialization), workspace, workspace_size,
        source, length, selection, selection_size, values, value_count, value_record_size,
        flags, outputs != NULL ? &outputs->target : NULL, outputs != NULL ? sizeof(outputs->target) : 0u,
        &result->target, sizeof(result->target), &sampling);
    result->required_pairs = sampling.count;
    if (status == DOE_COMPILER_OK && (flags & DOE_COMPILER_MODULE_MEASURE) == 0u)
        result->published_pairs = sampling.count;
    errno = saved_errno;
    return status;
}

DoeCompilerStatus doe_compiler_specialize_sampling(uint32_t version, uint32_t target,
    const DoeCompilerSamplingOptionsV16 *options, size_t options_size,
    const DoeCompilerSamplingCapacitiesV16 *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerSamplingOutputsV16 *outputs, size_t outputs_size,
    DoeCompilerSamplingResultV16 *result, size_t result_size) {
    return sampling_compile(version,target,options,options_size,caps,capacities_size,
        workspace,workspace_size,source,length,selection,selection_size,values,value_count,
        value_record_size,flags,outputs,outputs_size,result,result_size,NULL);
}
static DoeCompilerStatus translation_compile(uint32_t version, uint32_t target,
    const DoeCompilerSamplingOptionsV16 *options, size_t options_size,
    const DoeCompilerTranslationCapacitiesV17 *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerTranslationOutputsV17 *outputs, size_t outputs_size,
    DoeCompilerTranslationResultV17 *result, size_t result_size, DoeCompilerHlslProfileV19 *profile) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_TRANSLATION_ABI_VERSION || capacities_size != sizeof(*caps) ||
        result_size != sizeof(*result) || (outputs != NULL ? outputs_size != sizeof(*outputs) ||
            outputs->entry_record_size != sizeof(DoeCompilerTranslationEntryV17) : outputs_size != 0u))
        return DOE_COMPILER_ABI_MISMATCH;
    const int saved_errno = errno;
    memset(result,0,sizeof(*result));
    DoeCompilerWorkspaceRequirements requirements;
    size_t base;
    if (!translation_layout(target,caps,&base,&requirements) || workspace == NULL ||
        workspace_size < requirements.size_bytes || (uintptr_t)workspace % requirements.alignment_bytes != 0u ||
        (outputs != NULL && !compiler_valid_span(outputs->entries,outputs->entry_capacity,
            sizeof(*outputs->entries),_Alignof(DoeCompilerTranslationEntryV17),true))) {
        Source context = {.source=source,.length=length,.diagnostic=&result->sampling.target.stages.resources.module.diagnostic};
        (void)source_error(&context,DOE_COMPILER_INPUT_ERROR,0u,"invalid translation specialization layout or spans");
        errno = saved_errno;
        return DOE_COMPILER_INPUT_ERROR;
    }
    TranslationContext translation = {.staging=(DoeCompilerTranslationEntryV17 *)((unsigned char *)workspace+base),
        .capacity=caps->translation_entries,.outputs=outputs,.hlsl_profile=profile};
    const DoeCompilerStatus status = sampling_compile(DOE_COMPILER_SAMPLING_ABI_VERSION,target,options,options_size,
        &caps->sampling,sizeof(caps->sampling),workspace,workspace_size,source,length,selection,selection_size,
        values,value_count,value_record_size,flags,outputs != NULL ? &outputs->sampling : NULL,
        outputs != NULL ? sizeof(outputs->sampling) : 0u,&result->sampling,sizeof(result->sampling),&translation);
    result->required_entries = translation.count;
    if (status == DOE_COMPILER_OK && (flags & DOE_COMPILER_MODULE_MEASURE) == 0u)
        result->published_entries = translation.count;
    errno = saved_errno;
    return status;
}

DoeCompilerStatus doe_compiler_specialize_translation(uint32_t version, uint32_t target,
    const DoeCompilerSamplingOptionsV16 *options, size_t options_size,
    const DoeCompilerTranslationCapacitiesV17 *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerTranslationOutputsV17 *outputs, size_t outputs_size,
    DoeCompilerTranslationResultV17 *result, size_t result_size) {
    return translation_compile(version,target,options,options_size,caps,capacities_size,
        workspace,workspace_size,source,length,selection,selection_size,values,value_count,
        value_record_size,flags,outputs,outputs_size,result,result_size,NULL);
}

DoeCompilerStatus doe_compiler_specialize_hlsl_profile(uint32_t version,
    const DoeCompilerSamplingOptionsV16 *options, size_t options_size,
    const DoeCompilerTranslationCapacitiesV17 *caps, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size,
    const DoeCompilerOverrideValueV13 *values, size_t value_count, size_t value_record_size,
    uint32_t flags, const DoeCompilerTranslationOutputsV17 *outputs, size_t outputs_size,
    DoeCompilerHlslProfileResultV19 *result, size_t result_size) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_HLSL_PROFILE_ABI_VERSION || result_size != sizeof(*result) ||
        options_size != sizeof(*options) || capacities_size != sizeof(*caps) ||
        selection_size != sizeof(*selection) || value_record_size != sizeof(*values) ||
        (outputs != NULL ? outputs_size != sizeof(*outputs) ||
            outputs->entry_record_size != sizeof(DoeCompilerTranslationEntryV17) : outputs_size != 0u) ||
        !output_records(DOE_COMPILER_TARGET_HLSL,outputs != NULL ? &outputs->sampling.target : NULL))
        return DOE_COMPILER_ABI_MISMATCH;
    const int saved_errno = errno;
    memset(result,0,sizeof(*result));
    DoeCompilerHlslProfileV19 profile = {0};
    const DoeCompilerStatus status = translation_compile(DOE_COMPILER_TRANSLATION_ABI_VERSION,
        DOE_COMPILER_TARGET_HLSL,options,options_size,caps,capacities_size,
        workspace,workspace_size,source,length,selection,selection_size,values,value_count,
        value_record_size,flags,outputs,outputs_size,&result->translation,sizeof(result->translation),&profile);
    if (status == DOE_COMPILER_OK) result->profile = profile;
    errno = saved_errno;
    return status;
}
