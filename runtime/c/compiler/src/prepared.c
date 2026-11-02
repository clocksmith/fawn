/* SPDX-License-Identifier: MIT
 * Caller-owned continuation of one analyzed compiler workspace. */
#include "prepared_internal.h"
#include "text_internal.h"
#include <errno.h>
#include <string.h>

#define PREPARED_MARKER UINT64_C(0x444f455052455032)
static DoeCompilerStatus finish(DoeCompilerStatus status, int saved_errno)
{
    errno = saved_errno;
    return status;
}
static bool options_valid(const DoeCompilerPrepareOptionsV20 *options)
{
    const uint32_t semantics = DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT | DOE_COMPILER_CORE_BINDING_NUMBERS |
        DOE_COMPILER_DISABLE_F16 | DOE_COMPILER_DISABLE_SUBGROUPS | DOE_COMPILER_DISABLE_CLIP_DISTANCES |
        DOE_COMPILER_DISABLE_PRIMITIVE_INDEX | DOE_COMPILER_DISABLE_DUAL_SOURCE_BLENDING;
    return options && options->kind <= DOE_COMPILER_PREPARE_SPECIALIZED &&
        options->target <= DOE_COMPILER_TARGET_HLSL && !options->reserved &&
        !(options->semantic_flags & ~semantics) && options->selection.mode <= DOE_COMPILER_SELECT_NAME &&
        !options->selection.reserved &&
        (options->kind == DOE_COMPILER_PREPARE_SPECIALIZED || (!options->values && !options->value_count)) &&
        (options->kind != DOE_COMPILER_PREPARE_MODULE || options->selection.mode == DOE_COMPILER_SELECT_NONE) &&
        (options->kind != DOE_COMPILER_PREPARE_SPECIALIZED || options->selection.mode != DOE_COMPILER_SELECT_NONE) &&
        !(options->sampling.flags & ~DOE_COMPILER_SAMPLING_METAL_PIPELINE) &&
        (!options->sampling.flags || (options->kind == DOE_COMPILER_PREPARE_SPECIALIZED &&
            options->target == DOE_COMPILER_TARGET_METAL)) &&
        (options->sampling.flags || options->sampling.metal_sample_mask == UINT32_MAX) &&
        options->capacities.sampling.specialization.target.text_bytes == 0u;
}
static bool layout(const DoeCompilerPrepareOptionsV20 *options, size_t *offset,
    DoeCompilerWorkspaceRequirements *body, DoeCompilerWorkspaceRequirements *total)
{
    if (!options_valid(options)) return false;
    const DoeCompilerTextCapacities *target = &options->capacities.sampling.specialization.target;
    size_t base;
    if (options->kind == DOE_COMPILER_PREPARE_MODULE) {
        if (!specialization_layout(DOE_COMPILER_TARGET_SPIRV,
                &options->capacities.sampling.specialization, &base, body)) return false;
    } else if (options->kind == DOE_COMPILER_PREPARE_SPECIALIZED) {
        if (!translation_layout(options->target, &options->capacities, &base, body)) return false;
    } else if (options->target == DOE_COMPILER_TARGET_SPIRV) {
        CompilerLimits limits;
        if (!stage_layout(&target->stages, &limits, &base, &body->size_bytes)) return false;
        body->alignment_bytes = _Alignof(max_align_t);
    } else if (!compiler_text_workspace(options->target, target, body)) return false;
    const size_t padding = (body->alignment_bytes - sizeof(CompilerPrepared) % body->alignment_bytes) % body->alignment_bytes;
    *offset = sizeof(CompilerPrepared) + padding;
    if (body->size_bytes > SIZE_MAX - *offset) return false;
    *total = (DoeCompilerWorkspaceRequirements){*offset + body->size_bytes, _Alignof(max_align_t)};
    return true;
}
DoeCompilerStatus doe_compiler_prepare_requirements(uint32_t version,
    const DoeCompilerPrepareOptionsV20 *options, size_t options_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size)
{
    const int saved_errno = errno;
    if (!requirements) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_PREPARED_ABI_VERSION || options_size != sizeof(*options) ||
        requirements_size != sizeof(*requirements) ||
        (options && options->value_record_size != sizeof(DoeCompilerOverrideValueV13)))
        return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    size_t offset;
    DoeCompilerWorkspaceRequirements body, total;
    if (!layout(options, &offset, &body, &total)) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = total;
    return finish(DOE_COMPILER_OK, saved_errno);
}
void compiler_prepared_capture(CompilerPrepared *prepared, const Source *source,
    const CompilerMetadata *metadata, uint32_t selected)
{
    prepared->source = *source;
    prepared->selected = selected;
    if (metadata) prepared->metadata = *metadata;
    if (!source->specialization) return;
    prepared->specialization = *source->specialization;
    prepared->source.specialization = &prepared->specialization;
    prepared->specialization.capacities = &prepared->options.capacities.sampling.specialization;
    prepared->specialization.selection = prepared->options.kind == DOE_COMPILER_PREPARE_MODULE ?
        NULL : &prepared->options.selection;
    if (!source->specialization->sampling) return;
    prepared->sampling = *source->specialization->sampling;
    prepared->specialization.sampling = &prepared->sampling;
    prepared->metadata.sampling = &prepared->sampling;
    if (!prepared->sampling.translation) return;
    prepared->translation = *prepared->sampling.translation;
    prepared->sampling.translation = &prepared->translation;
    prepared->translation.hlsl_profile = prepared->options.target == DOE_COMPILER_TARGET_HLSL ?
        &prepared->result.compiled.profile : NULL;
}
DoeCompilerStatus doe_compiler_prepare(uint32_t version,
    const DoeCompilerPrepareOptionsV20 *options, size_t options_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    DoeCompilerPreparedResultV20 *result, size_t result_size)
{
    const int saved_errno = errno;
    if (!result) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_PREPARED_ABI_VERSION || options_size != sizeof(*options) ||
        result_size != sizeof(*result) ||
        (options && options->value_record_size != sizeof(DoeCompilerOverrideValueV13)))
        return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    memset(result, 0, sizeof(*result));
    size_t offset;
    DoeCompilerWorkspaceRequirements body, total;
    if (!layout(options, &offset, &body, &total) || !workspace || workspace_size < total.size_bytes ||
        (uintptr_t)workspace % total.alignment_bytes != 0u) {
        result->compiled.translation.sampling.target.stages.resources.module.diagnostic.status = DOE_COMPILER_INPUT_ERROR;
        result->validation.validation.diagnostic.status = DOE_COMPILER_INPUT_ERROR;
        return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    }
    CompilerPrepared *prepared = workspace;
    memset(prepared, 0, sizeof(*prepared));
    prepared->options = *options;
    prepared->body_offset = offset;
    prepared->body_bytes = body.size_bytes;
    prepared->total_bytes = total.size_bytes;
    prepared->flags = DOE_COMPILER_MODULE_WORDS | DOE_COMPILER_MODULE_REFLECTION | options->semantic_flags;
    void *memory = (unsigned char *)workspace + offset;
    DoeCompilerTextResult *compiled = &prepared->result.compiled.translation.sampling.target;
    DoeCompilerStatus status;
    if (options->kind == DOE_COMPILER_PREPARE_MODULE)
        status = compiler_prepare_validation(prepared, memory, body.size_bytes, source, length);
    else if (options->kind == DOE_COMPILER_PREPARE_SPECIALIZED)
        status = compiler_prepare_translation(prepared, memory, body.size_bytes, source, length);
    else if (options->target != DOE_COMPILER_TARGET_SPIRV)
        status = compiler_prepare_text(prepared, memory, body.size_bytes, source, length);
    else {
        const DoeCompilerStageCapacities *stages = &prepared->options.capacities.sampling.specialization.target.stages;
        status = compiler_compile_module(&stages->module, memory, body.size_bytes, source, length,
            &prepared->options.selection, prepared->flags | DOE_COMPILER_MODULE_MEASURE, NULL,
            &compiled->stages.resources.module, NULL, &compiled->stages.resources, stages, NULL,
            &compiled->stages, DOE_COMPILER_ATOMIC_ABI_VERSION, NULL, prepared);
    }
    if (status == DOE_COMPILER_OK) {
        prepared->result.publication_bytes = options->kind == DOE_COMPILER_PREPARE_MODULE ? 0u : compiled->required_text_bytes;
        prepared->marker = PREPARED_MARKER;
    }
    *result = prepared->result;
    return finish(status, saved_errno);
}
static CompilerPrepared *ready(void *workspace, size_t size)
{
    if (!workspace || size < sizeof(CompilerPrepared) || (uintptr_t)workspace % _Alignof(max_align_t)) return NULL;
    CompilerPrepared *prepared = workspace;
    return prepared->marker == PREPARED_MARKER && prepared->total_bytes <= size ? prepared : NULL;
}
DoeCompilerStatus doe_compiler_publish_module(uint32_t version,
    void *workspace, size_t size, const DoeCompilerDiagnosticOutputsV18 *outputs, size_t outputs_size,
    DoeCompilerPreparedResultV20 *result, size_t result_size)
{
    const int saved_errno = errno;
    if (!result) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_PREPARED_ABI_VERSION || result_size != sizeof(*result) ||
        outputs_size != sizeof(*outputs) || (outputs &&
            (outputs->metadata.override_record_size != sizeof(DoeCompilerOverrideV13) ||
             outputs->metadata.entry_record_size != sizeof(DoeCompilerModuleEntryV13) ||
             outputs->notice_record_size != sizeof(DoeCompilerNoticeV18))))
        return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerPrepared *prepared = ready(workspace, size);
    if (!prepared || prepared->options.kind != DOE_COMPILER_PREPARE_MODULE || !outputs)
        return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    const DoeCompilerPreparedResultV20 saved = prepared->result;
    const DoeCompilerStatus status = compiler_publish_validation(prepared, outputs);
    *result = prepared->result;
    prepared->result = saved;
    return finish(status, saved_errno);
}
static bool output_records(uint32_t target, const DoeCompilerTranslationOutputsV17 *outputs)
{
    const DoeCompilerSpecializationOutputs *out = &outputs->sampling.target;
    const DoeCompilerStageOutputs *stages = target == DOE_COMPILER_TARGET_METAL ? &out->metal.stages :
        target == DOE_COMPILER_TARGET_HLSL ? &out->hlsl.stages : &out->spirv;
    return compiler_module_records(&stages->resources.module) &&
        stages->resources.handle_record_size == sizeof(DoeCompilerHandleV6) &&
        stages->interface_record_size == sizeof(DoeCompilerStageInterfaceV7) &&
        outputs->sampling.pair_record_size == sizeof(DoeCompilerSamplingPairV16) &&
        outputs->entry_record_size == sizeof(DoeCompilerTranslationEntryV17) &&
        (target != DOE_COMPILER_TARGET_METAL || (out->metal.binding_record_size == sizeof(DoeCompilerMetalBindingV12) &&
            out->metal.entry_record_size == sizeof(DoeCompilerMetalEntryV12))) &&
        (target != DOE_COMPILER_TARGET_HLSL || (out->hlsl.binding_record_size == sizeof(DoeCompilerHlslBindingV11) &&
            out->hlsl.entry_record_size == sizeof(DoeCompilerHlslEntryV11)));
}
DoeCompilerStatus doe_compiler_publish(uint32_t version,
    void *workspace, size_t size, void *scratch, size_t scratch_size,
    const DoeCompilerTranslationOutputsV17 *outputs, size_t outputs_size,
    DoeCompilerPreparedResultV20 *result, size_t result_size)
{
    const int saved_errno = errno;
    if (!result) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_PREPARED_ABI_VERSION || result_size != sizeof(*result) ||
        outputs_size != sizeof(*outputs)) return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerPrepared *prepared = ready(workspace, size);
    if (!prepared || prepared->options.kind == DOE_COMPILER_PREPARE_MODULE || !outputs)
        return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    const uint32_t target = prepared->options.target;
    if (!output_records(target, outputs)) return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    const DoeCompilerPreparedResultV20 saved = prepared->result;
    DoeCompilerTextResult *compiled = &prepared->result.compiled.translation.sampling.target;
    Source *source = &prepared->source;
    DoeCompilerStatus status = DOE_COMPILER_INPUT_ERROR;
    prepared->sampling.outputs = &outputs->sampling;
    prepared->translation.outputs = outputs;
    if (!compiler_valid_span(outputs->sampling.pairs, outputs->sampling.pair_capacity,
            sizeof(*outputs->sampling.pairs), _Alignof(DoeCompilerSamplingPairV16), true) ||
        !compiler_valid_span(outputs->entries, outputs->entry_capacity,
            sizeof(*outputs->entries), _Alignof(DoeCompilerTranslationEntryV17), true))
        (void)source_error(source, status, 0u, "invalid retained translation output spans");
    else if (target != DOE_COMPILER_TARGET_SPIRV)
        status = compiler_publish_text(prepared, outputs, scratch, scratch_size);
    else {
        const DoeCompilerStageOutputs *stages = &outputs->sampling.target.spirv;
        if (!compiler_module_spans(source, prepared->flags, &stages->resources.module) ||
            !compiler_valid_span(stages->resources.handles, stages->resources.handle_capacity,
                sizeof(DoeCompilerHandleV6), _Alignof(DoeCompilerHandleV6), true) ||
            !compiler_valid_span(stages->interfaces, stages->interface_capacity,
                sizeof(DoeCompilerStageInterfaceV7), _Alignof(DoeCompilerStageInterfaceV7), true))
            (void)source_error(source, status, 0u, "invalid retained stage output spans");
        else if (!compiler_metadata_fits(&prepared->metadata, &stages->resources.module, &stages->resources, stages)) {
            status = DOE_COMPILER_OUTPUT_TOO_SMALL;
            (void)source_error(source, status, source->length, "retained stage output capacity is too small");
        } else {
            const size_t words = prepared->metadata.counts.words;
            if (words) memcpy(stages->resources.module.words, source->compiler->words, words * sizeof(uint32_t));
            compiler_metadata_publish(source, &prepared->metadata, prepared->selected, DOE_COMPILER_ATOMIC_ABI_VERSION,
                &stages->resources.module, &compiled->stages.resources.module,
                &stages->resources, &compiled->stages.resources, stages, &compiled->stages);
            compiled->stages.resources.module.published = prepared->metadata.counts;
            status = DOE_COMPILER_OK;
        }
    }
    if (status == DOE_COMPILER_OK && prepared->options.kind == DOE_COMPILER_PREPARE_SPECIALIZED) {
        prepared->result.compiled.translation.sampling.published_pairs = prepared->sampling.count;
        prepared->result.compiled.translation.published_entries = prepared->translation.count;
    }
    *result = prepared->result;
    prepared->result = saved;
    prepared->sampling.outputs = NULL;
    prepared->translation.outputs = NULL;
    return finish(status, saved_errno);
}
