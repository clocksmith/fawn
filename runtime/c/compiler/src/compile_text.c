/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "text_internal.h"
#include "override.h"
#include <errno.h>
#include <string.h>

typedef struct {
    size_t stage_base, closure, closure_bytes, bindings, entries, text, total;
} TextLayout;
typedef struct {
    DoeCompilerStageOutputs stages;
    char *text;
    size_t text_capacity, binding_capacity, entry_capacity;
    union {
        struct { DoeCompilerTextBindingV10 *bindings; DoeCompilerTextEntryV10 *entries; } metal;
        struct { DoeCompilerMetalBindingV12 *bindings; DoeCompilerMetalEntryV12 *entries; } metal12;
        struct { DoeCompilerHlslBindingV11 *bindings; DoeCompilerHlslEntryV11 *entries; } hlsl;
    } value;
} TextOutputs;
static bool append(size_t *cursor, size_t count, size_t width, size_t alignment, size_t *offset) {
    const size_t padding = (alignment - *cursor % alignment) % alignment;
    if (padding > SIZE_MAX - *cursor) return false;
    *cursor += padding;
    *offset = *cursor;
    if (count > (SIZE_MAX - *cursor) / width) return false;
    *cursor += count * width;
    return true;
}
static bool text_layout(uint32_t target, const DoeCompilerTextCapacities *caps, CompilerLimits *limits, TextLayout *layout, bool dense_metal) {
    if (caps == NULL || !stage_layout(&caps->stages, limits, &layout->stage_base, &layout->total)) return false;
    const uint64_t closure = (uint64_t)limits->tokens / 6u + limits->types + IR_PRIMITIVE_COUNT;
    if (closure > SIZE_MAX) return false;
    layout->closure_bytes = (size_t)closure;
    const bool hlsl = target == DOE_COMPILER_TARGET_HLSL;
    const size_t binding_size = dense_metal ? sizeof(DoeCompilerMetalBindingV12) : hlsl ? sizeof(DoeCompilerHlslBindingV11) : sizeof(DoeCompilerTextBindingV10);
    const size_t entry_size = dense_metal ? sizeof(DoeCompilerMetalEntryV12) : hlsl ? sizeof(DoeCompilerHlslEntryV11) : sizeof(DoeCompilerTextEntryV10);
    const size_t binding_alignment = dense_metal ? _Alignof(DoeCompilerMetalBindingV12) : hlsl ? _Alignof(DoeCompilerHlslBindingV11) : _Alignof(DoeCompilerTextBindingV10);
    const size_t entry_alignment = dense_metal ? _Alignof(DoeCompilerMetalEntryV12) : hlsl ? _Alignof(DoeCompilerHlslEntryV11) : _Alignof(DoeCompilerTextEntryV10);
    return append(&layout->total, layout->closure_bytes, 1u, 1u, &layout->closure) &&
        append(&layout->total, dense_metal ? limits->entry_references : limits->resources, binding_size, binding_alignment, &layout->bindings) &&
        append(&layout->total, limits->entries, entry_size, entry_alignment, &layout->entries) &&
        append(&layout->total, caps->text_bytes, 1u, 1u, &layout->text);
}
bool compiler_text_workspace(uint32_t target, const DoeCompilerTextCapacities *caps,
                             DoeCompilerWorkspaceRequirements *requirements) {
    CompilerLimits limits;
    TextLayout layout;
    if ((target != DOE_COMPILER_TARGET_METAL && target != DOE_COMPILER_TARGET_HLSL) ||
        !text_layout(target, caps, &limits, &layout, target == DOE_COMPILER_TARGET_METAL)) return false;
    *requirements = (DoeCompilerWorkspaceRequirements){layout.total, _Alignof(max_align_t)};
    return true;
}
static DoeCompilerStatus finish(DoeCompilerStatus status, int saved_errno) {
    errno = saved_errno;
    return status;
}
DoeCompilerStatus doe_compiler_text_requirements(uint32_t version, uint32_t target,
    const DoeCompilerTextCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    if (requirements == NULL) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_TEXT_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        requirements_size != sizeof(*requirements)) return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    if (target != DOE_COMPILER_TARGET_METAL) return finish(DOE_COMPILER_UNSUPPORTED, saved_errno);
    CompilerLimits limits;
    TextLayout layout;
    if (!text_layout(target, capacities, &limits, &layout, false)) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){layout.total, _Alignof(max_align_t)};
    return finish(DOE_COMPILER_OK, saved_errno);
}
static bool text_records(const DoeCompilerTextOutputs *outputs) {
    if (outputs == NULL) return true;
    return compiler_module_records(&outputs->stages.resources.module) &&
        outputs->stages.resources.handle_record_size == sizeof(DoeCompilerHandleV6) &&
        outputs->stages.interface_record_size == sizeof(DoeCompilerStageInterfaceV7) &&
        outputs->binding_record_size == sizeof(DoeCompilerTextBindingV10) &&
        outputs->entry_record_size == sizeof(DoeCompilerTextEntryV10);
}
static bool text_spans(Source *s, uint32_t target, uint32_t flags, const TextOutputs *outputs, bool dense_metal) {
    const bool measure = (flags & DOE_COMPILER_TEXT_MEASURE) != 0u;
    const bool code = (flags & DOE_COMPILER_TEXT_CODE) != 0u;
    const bool reflection = (flags & DOE_COMPILER_TEXT_REFLECTION) != 0u;
    if ((flags & 3u) == 0u || (flags & ~(7u | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT | DOE_COMPILER_CORE_BINDING_NUMBERS | DOE_COMPILER_DISABLE_F16 | DOE_COMPILER_DISABLE_SUBGROUPS)) != 0u ||
        measure != (outputs == NULL))
        return source_error(s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid text output flags or measure descriptor");
    if (measure) return true;
    const DoeCompilerStageOutputs *stages = &outputs->stages;
    /* The common module validator requires one selected output category. With
     * neither words nor reflection, validate its empty spans as WORDS but then
     * require the word span itself to be empty as this API never emits SPIR-V. */
    if (!compiler_module_spans(s, reflection ? DOE_COMPILER_MODULE_REFLECTION : DOE_COMPILER_MODULE_WORDS,
        &stages->resources.module)) return false;
    if (stages->resources.module.words != NULL || stages->resources.module.word_capacity != 0u ||
        !compiler_valid_span(outputs->text, outputs->text_capacity, 1u, 1u, code) ||
        !compiler_valid_span(stages->resources.handles, stages->resources.handle_capacity,
            sizeof(DoeCompilerHandleV6), _Alignof(DoeCompilerHandleV6), reflection) ||
        !compiler_valid_span(stages->interfaces, stages->interface_capacity,
            sizeof(DoeCompilerStageInterfaceV7), _Alignof(DoeCompilerStageInterfaceV7), reflection) ||
        (dense_metal ?
            (!compiler_valid_span(outputs->value.metal12.bindings, outputs->binding_capacity,
                sizeof(DoeCompilerMetalBindingV12), _Alignof(DoeCompilerMetalBindingV12), reflection) ||
             !compiler_valid_span(outputs->value.metal12.entries, outputs->entry_capacity,
                sizeof(DoeCompilerMetalEntryV12), _Alignof(DoeCompilerMetalEntryV12), reflection)) :
         target == DOE_COMPILER_TARGET_METAL ?
            (!compiler_valid_span(outputs->value.metal.bindings, outputs->binding_capacity,
                sizeof(DoeCompilerTextBindingV10), _Alignof(DoeCompilerTextBindingV10), reflection) ||
             !compiler_valid_span(outputs->value.metal.entries, outputs->entry_capacity,
                sizeof(DoeCompilerTextEntryV10), _Alignof(DoeCompilerTextEntryV10), reflection)) :
            (!compiler_valid_span(outputs->value.hlsl.bindings, outputs->binding_capacity,
                sizeof(DoeCompilerHlslBindingV11), _Alignof(DoeCompilerHlslBindingV11), reflection) ||
             !compiler_valid_span(outputs->value.hlsl.entries, outputs->entry_capacity,
                sizeof(DoeCompilerHlslEntryV11), _Alignof(DoeCompilerHlslEntryV11), reflection))))
        return source_error(s, DOE_COMPILER_INPUT_ERROR, 0u, "invalid selected text output span or capacity product");
    return true;
}
static bool lower_text(uint32_t target_kind, Source *source, const DoeCompilerTextCapacities *caps, const TextLayout *layout,
    const DoeCompilerEntrySelectionV5 *selection, uint32_t flags, const TextOutputs *outputs,
    DoeCompilerTextResult *result, CompilerMetadata *metadata, uint32_t *selected, bool dense_metal) {
    CompilerWorkspace *c = source->compiler;
    unsigned char *memory = (unsigned char *)c;
    DoeTargetScratch scratch = {memory + layout->closure, layout->closure_bytes};
    TextTargetResult target;
    DoeCompilerTypedOutputCounts typed;
    if (!compiler_select_entry(source, selection, selected) ||
        !(dense_metal ?
            compiler_text_metal_entry(source, *selected, scratch,
                (DoeCompilerMetalBindingV12 *)(memory + layout->bindings),
                (DoeCompilerMetalEntryV12 *)(memory + layout->entries), &target) :
          target_kind == DOE_COMPILER_TARGET_METAL ?
            compiler_text_metal(source, *selected, scratch,
                (DoeCompilerTextBindingV10 *)(memory + layout->bindings),
                (DoeCompilerTextEntryV10 *)(memory + layout->entries), &target) :
            compiler_text_hlsl(source, *selected, scratch,
                (DoeCompilerHlslBindingV11 *)(memory + layout->bindings),
                (DoeCompilerHlslEntryV11 *)(memory + layout->entries), &target)) ||
        !reflect_prepare(source, c, &typed) ||
        !compiler_metadata_prepare(source, &typed, flags & DOE_COMPILER_TEXT_REFLECTION, true, metadata)) return false;
    const bool code = (flags & DOE_COMPILER_TEXT_CODE) != 0u;
    const bool reflection = (flags & DOE_COMPILER_TEXT_REFLECTION) != 0u;
    DoeCompilerModuleResult *module = &result->stages.resources.module;
    module->required = metadata->counts;
    result->stages.required_interfaces = metadata->interfaces;
    result->stages.resources.required_handles = metadata->handles;
    result->required_text_bytes = code ? target.required_bytes : 0u;
    result->required_bindings = reflection ? target.bindings : 0u;
    result->required_entries = reflection ? target.entries : 0u;
    module->required_is_exact = 1u;
    if ((flags & DOE_COMPILER_TEXT_MEASURE) != 0u) return true;
    if (!compiler_metadata_fits(metadata, &outputs->stages.resources.module,
            &outputs->stages.resources, &outputs->stages) ||
        outputs->text_capacity < result->required_text_bytes ||
        outputs->binding_capacity < result->required_bindings || outputs->entry_capacity < result->required_entries)
        return source_error(source, DOE_COMPILER_OUTPUT_TOO_SMALL, source->length,
            "selected text caller output capacity is too small");
    if (caps->text_bytes < result->required_text_bytes)
        return source_pool_error(source, DOE_COMPILER_POOL_TEXT_BYTES, result->required_text_bytes,
            source->length, "text staging capacity is too small");
    if (code && target.required_bytes != 0u) {
        size_t written_bytes;
        if (dense_metal) {
            DoeMetalMetadata plan = {.bindings=(DoeCompilerMetalBindingV12 *)(memory + layout->bindings),
                .binding_capacity=c->limits.entry_references,.binding_count=target.bindings,
                .entries=(DoeCompilerMetalEntryV12 *)(memory + layout->entries),
                .entry_capacity=c->limits.entries,.entry_count=target.entries};
            DoeMetalResult written;
            if (doe_metal_emit_dense(source, c, *selected, scratch, &plan, (char *)memory + layout->text,
                    caps->text_bytes, &written) != DOE_COMPILER_OK) return false;
            written_bytes = written.written_bytes;
        } else if (target_kind == DOE_COMPILER_TARGET_METAL) {
            DoeMetalResult written;
            if (doe_metal_emit(source, c, *selected, scratch, (char *)memory + layout->text,
                    caps->text_bytes, &written) != DOE_COMPILER_OK) return false;
            written_bytes = written.written_bytes;
        } else {
            DoeHlslResult written;
            if (doe_hlsl_emit(source, c, *selected, scratch, (char *)memory + layout->text,
                    caps->text_bytes, &written) != DOE_COMPILER_OK) return false;
            written_bytes = written.written_bytes;
        }
        if (written_bytes != target.required_bytes)
            return source_error(source, DOE_COMPILER_INPUT_ERROR, source->length,
                "text serialization changed after successful measurement");
    }
    return true;
}
static DoeCompilerStatus compile_text(uint32_t target,
    const DoeCompilerTextCapacities *capacities, void *workspace, size_t workspace_size,
    const char *source, size_t length, const DoeCompilerEntrySelectionV5 *selection,
    uint32_t flags, const TextOutputs *outputs, DoeCompilerTextResult *result, bool dense_metal, SpecializationContext *specialization) {
    const int saved_errno = errno;
    memset(result, 0, sizeof(*result));
    DoeCompilerModuleResult *module = &result->stages.resources.module;
    Source context = {.source = source, .length = length, .diagnostic = &module->diagnostic,
        .exhausted_pool = &module->exhausted_pool, .minimum_pool_capacity = &module->minimum_pool_capacity, .specialization=specialization};
    CompilerLimits limits;
    TextLayout layout;
    if (target != DOE_COMPILER_TARGET_METAL && target != DOE_COMPILER_TARGET_HLSL) {
        (void)source_error(&context, DOE_COMPILER_UNSUPPORTED, 0u, "unsupported text target");
        return finish(module->diagnostic.status, saved_errno);
    }
    if (!text_layout(target, capacities, &limits, &layout, dense_metal) || workspace == NULL || workspace_size < layout.total ||
        (uintptr_t)workspace % _Alignof(max_align_t) != 0u) {
        (void)source_error(&context, DOE_COMPILER_INPUT_ERROR, 0u, "invalid text capacities or workspace size/alignment");
        return finish(module->diagnostic.status, saved_errno);
    }
    if (!compiler_selection_shape(&context, selection) || !text_spans(&context, target, flags, outputs, dense_metal))
        return finish(module->diagnostic.status, saved_errno);
    CompilerWorkspace *c = workspace;
    compiler_job_bind(c, workspace, &limits);
    c->module_flags = ((flags & DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT) != 0u ? MODULE_STANDARD_UNIFORM : 0u) |
        ((flags & DOE_COMPILER_CORE_BINDING_NUMBERS) != 0u ? MODULE_CORE_BINDINGS : 0u) |
        ((flags & DOE_COMPILER_DISABLE_F16) != 0u ? MODULE_DISABLE_F16 : 0u) |
        ((flags & DOE_COMPILER_DISABLE_SUBGROUPS) != 0u ? MODULE_DISABLE_SUBGROUPS : 0u);
    stage_bind(&context, workspace, &capacities->stages, layout.stage_base);
    if (specialization != NULL) specialization->stage_base = layout.stage_base;
    NumericEnvironment environment;
    CompilerMetadata metadata;
    uint32_t selected = NO_ID;
    const bool lowered = compiler_analyze(&context, c, &environment) &&
        lower_text(target, &context, capacities, &layout, selection, flags, outputs, result, &metadata, &selected, dense_metal);
    const bool restored = numeric_environment_end(&context, &environment);
    if (!lowered || !restored) return finish(module->diagnostic.status, saved_errno);
    result->target = target;
    result->language_version = target == DOE_COMPILER_TARGET_METAL ? 300u : 600u;
    if ((flags & DOE_COMPILER_TEXT_MEASURE) != 0u) return finish(DOE_COMPILER_OK, saved_errno);
    const unsigned char *memory = workspace;
    if (result->required_text_bytes != 0u)
        memcpy(outputs->text, memory + layout.text, result->required_text_bytes);
    if ((flags & DOE_COMPILER_TEXT_REFLECTION) != 0u) {
        compiler_metadata_publish(&context, &metadata, selected, DOE_COMPILER_ATOMIC_SCHEMA_VERSION,
            &outputs->stages.resources.module, module, &outputs->stages.resources, &result->stages.resources,
            &outputs->stages, &result->stages);
        if (dense_metal) {
            if (result->required_bindings != 0u)
                memcpy(outputs->value.metal12.bindings, memory + layout.bindings,
                    result->required_bindings * sizeof(*outputs->value.metal12.bindings));
            if (result->required_entries != 0u)
                memcpy(outputs->value.metal12.entries, memory + layout.entries,
                    result->required_entries * sizeof(*outputs->value.metal12.entries));
        } else if (target == DOE_COMPILER_TARGET_METAL) {
            if (result->required_bindings != 0u)
                memcpy(outputs->value.metal.bindings, memory + layout.bindings,
                    result->required_bindings * sizeof(*outputs->value.metal.bindings));
            if (result->required_entries != 0u)
                memcpy(outputs->value.metal.entries, memory + layout.entries,
                    result->required_entries * sizeof(*outputs->value.metal.entries));
        } else {
            if (result->required_bindings != 0u)
                memcpy(outputs->value.hlsl.bindings, memory + layout.bindings,
                    result->required_bindings * sizeof(*outputs->value.hlsl.bindings));
            if (result->required_entries != 0u)
                memcpy(outputs->value.hlsl.entries, memory + layout.entries,
                    result->required_entries * sizeof(*outputs->value.hlsl.entries));
        }
    }
    result->published_text_bytes = result->required_text_bytes;
    result->published_bindings = result->required_bindings;
    result->published_entries = result->required_entries;
    module->published = metadata.counts;
    return finish(DOE_COMPILER_OK, saved_errno);
}

DoeCompilerStatus doe_compiler_compile_text(uint32_t version, uint32_t target,
    const DoeCompilerTextCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size, uint32_t flags,
    const DoeCompilerTextOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_TEXT_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        selection_size != sizeof(*selection) || result_size != sizeof(*result) ||
        (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u) || !text_records(outputs))
        return DOE_COMPILER_ABI_MISMATCH;
    TextOutputs normalized = {0};
    if (outputs != NULL) normalized = (TextOutputs){.stages=outputs->stages,.text=outputs->text,
        .text_capacity=outputs->text_capacity,.binding_capacity=outputs->binding_capacity,
        .entry_capacity=outputs->entry_capacity,
        .value.metal={outputs->bindings, outputs->entries}};
    /* ABI10 keeps its existing target admission, including unsupported result
     * initialization. Only the additive typed ABI11 facade selects HLSL. */
    return compile_text(target == DOE_COMPILER_TARGET_METAL ? target : 0u, capacities,
        workspace, workspace_size, source, length, selection, flags,
        outputs != NULL ? &normalized : NULL, result, false, NULL);
}

DoeCompilerStatus doe_compiler_hlsl_requirements(uint32_t version,
    const DoeCompilerTextCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    if (requirements == NULL) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_HLSL_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        requirements_size != sizeof(*requirements)) return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerLimits limits;
    TextLayout layout;
    if (!text_layout(DOE_COMPILER_TARGET_HLSL, capacities, &limits, &layout, false))
        return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){layout.total, _Alignof(max_align_t)};
    return finish(DOE_COMPILER_OK, saved_errno);
}

DoeCompilerStatus doe_compiler_compile_hlsl(uint32_t version,
    const DoeCompilerTextCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size, uint32_t flags,
    const DoeCompilerHlslOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_HLSL_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        selection_size != sizeof(*selection) || result_size != sizeof(*result) ||
        (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u))
        return DOE_COMPILER_ABI_MISMATCH;
    if (outputs != NULL && (!compiler_module_records(&outputs->stages.resources.module) ||
        outputs->stages.resources.handle_record_size != sizeof(DoeCompilerHandleV6) ||
        outputs->stages.interface_record_size != sizeof(DoeCompilerStageInterfaceV7) ||
        outputs->binding_record_size != sizeof(DoeCompilerHlslBindingV11) ||
        outputs->entry_record_size != sizeof(DoeCompilerHlslEntryV11))) return DOE_COMPILER_ABI_MISMATCH;
    TextOutputs normalized = {0};
    if (outputs != NULL) normalized = (TextOutputs){.stages=outputs->stages,.text=outputs->text,
        .text_capacity=outputs->text_capacity,.binding_capacity=outputs->binding_capacity,
        .entry_capacity=outputs->entry_capacity,
        .value.hlsl={outputs->bindings, outputs->entries}};
    return compile_text(DOE_COMPILER_TARGET_HLSL, capacities, workspace, workspace_size,
        source, length, selection, flags, outputs != NULL ? &normalized : NULL, result, false, NULL);
}

DoeCompilerStatus doe_compiler_metal_requirements(uint32_t version,
    const DoeCompilerTextCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    const int saved_errno = errno;
    if (requirements == NULL) return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    if (version != DOE_COMPILER_METAL_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        requirements_size != sizeof(*requirements)) return finish(DOE_COMPILER_ABI_MISMATCH, saved_errno);
    CompilerLimits limits;
    TextLayout layout;
    if (!text_layout(DOE_COMPILER_TARGET_METAL, capacities, &limits, &layout, true))
        return finish(DOE_COMPILER_INPUT_ERROR, saved_errno);
    *requirements = (DoeCompilerWorkspaceRequirements){layout.total, _Alignof(max_align_t)};
    return finish(DOE_COMPILER_OK, saved_errno);
}
DoeCompilerStatus doe_compiler_compile_metal(uint32_t version,
    const DoeCompilerTextCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t length,
    const DoeCompilerEntrySelectionV5 *selection, size_t selection_size, uint32_t flags,
    const DoeCompilerMetalOutputs *outputs, size_t outputs_size,
    DoeCompilerTextResult *result, size_t result_size) {
    if (result == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_METAL_ABI_VERSION || capacities_size != sizeof(*capacities) ||
        selection_size != sizeof(*selection) || result_size != sizeof(*result) ||
        (outputs != NULL ? outputs_size != sizeof(*outputs) : outputs_size != 0u))
        return DOE_COMPILER_ABI_MISMATCH;
    if (outputs != NULL && (!compiler_module_records(&outputs->stages.resources.module) ||
        outputs->stages.resources.handle_record_size != sizeof(DoeCompilerHandleV6) ||
        outputs->stages.interface_record_size != sizeof(DoeCompilerStageInterfaceV7) ||
        outputs->binding_record_size != sizeof(DoeCompilerMetalBindingV12) ||
        outputs->entry_record_size != sizeof(DoeCompilerMetalEntryV12))) return DOE_COMPILER_ABI_MISMATCH;
    TextOutputs normalized = {0};
    if (outputs != NULL) normalized = (TextOutputs){.stages=outputs->stages,.text=outputs->text,
        .text_capacity=outputs->text_capacity,.binding_capacity=outputs->binding_capacity,
        .entry_capacity=outputs->entry_capacity,.value.metal12={outputs->bindings, outputs->entries}};
    return compile_text(DOE_COMPILER_TARGET_METAL, capacities, workspace, workspace_size,
        source, length, selection, flags, outputs != NULL ? &normalized : NULL, result, true, NULL);
}

DoeCompilerStatus compiler_specialized_text(uint32_t target,
    const DoeCompilerTextCapacities *caps, void *workspace, size_t workspace_size,
    const char *source, size_t length, const DoeCompilerEntrySelectionV5 *selection,
    uint32_t flags, const DoeCompilerSpecializationOutputs *outputs,
    DoeCompilerTextResult *result, SpecializationContext *context) {
    TextOutputs normalized = {0};
    if (outputs != NULL) {
        if (target == DOE_COMPILER_TARGET_METAL) {
            const DoeCompilerMetalOutputs *out = &outputs->metal;
            normalized = (TextOutputs){.stages=out->stages,.text=out->text,
                .text_capacity=out->text_capacity,.binding_capacity=out->binding_capacity,
                .entry_capacity=out->entry_capacity,.value.metal12={out->bindings,out->entries}};
        } else {
            const DoeCompilerHlslOutputs *out = &outputs->hlsl;
            normalized = (TextOutputs){.stages=out->stages,.text=out->text,
                .text_capacity=out->text_capacity,.binding_capacity=out->binding_capacity,
                .entry_capacity=out->entry_capacity,.value.hlsl={out->bindings,out->entries}};
        }
    }
    return compile_text(target, caps, workspace, workspace_size, source, length, selection,
        flags, outputs != NULL ? &normalized : NULL, result, target == DOE_COMPILER_TARGET_METAL, context);
}
