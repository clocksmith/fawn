/* SPDX-License-Identifier: MIT */
#include "sampling_internal.h"
#include "override.h"
#include <errno.h>

bool sampling_layout(uint32_t target, const DoeCompilerSamplingCapacitiesV16 *caps,
    size_t *base, DoeCompilerWorkspaceRequirements *requirements) {
    size_t specialization_base;
    if (caps == NULL || caps->sampling_pairs >= UINT32_MAX ||
        !specialization_layout(target, &caps->specialization, &specialization_base, requirements)) return false;
    const size_t alignment = _Alignof(DoeCompilerSamplingPairV16);
    const size_t padding = (alignment - requirements->size_bytes % alignment) % alignment;
    if (requirements->size_bytes > SIZE_MAX - padding) return false;
    *base = requirements->size_bytes + padding;
    if (caps->sampling_pairs > (SIZE_MAX - *base) / sizeof(DoeCompilerSamplingPairV16)) return false;
    requirements->size_bytes = *base + caps->sampling_pairs * sizeof(DoeCompilerSamplingPairV16);
    return true;
}
DoeCompilerStatus doe_compiler_sampling_requirements(uint32_t version, uint32_t target,
    const DoeCompilerSamplingCapacitiesV16 *caps, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size) {
    if (requirements == NULL) return DOE_COMPILER_INPUT_ERROR;
    if (version != DOE_COMPILER_SAMPLING_ABI_VERSION || capacities_size != sizeof(*caps) ||
        requirements_size != sizeof(*requirements)) return DOE_COMPILER_ABI_MISMATCH;
    DoeCompilerWorkspaceRequirements result;
    size_t base;
    if (!sampling_layout(target, caps, &base, &result)) return DOE_COMPILER_INPUT_ERROR;
    *requirements = result;
    return DOE_COMPILER_OK;
}
SamplingContext *sampling_context(Source *s) {
    return s->specialization == NULL ? NULL : s->specialization->sampling;
}
static bool charge(Source *s) {
    SamplingContext *context = sampling_context(s);
    if (context->work++ == UINT64_C(67108864))
        return source_error(s, DOE_COMPILER_UNSUPPORTED, s->length, "sampling relationship work format exhausted");
    return true;
}
static bool push(Source *s, uint32_t function, uint32_t call) {
    CompilerWorkspace *c = s->compiler;
    if (c->walk_count == c->limits.traversal)
        return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count + 1u,
            s->length, "sampling call traversal capacity exceeded");
    c->walk[c->walk_count++] = (WalkItem){.object=function,
        .cursor=c->functions[function].instruction_start, .parent=call};
    return true;
}
/* Formal handles follow the exact enclosing call chain, not the union of
 * every caller's resources. WGSL does not allow handle-valued let aliases. */
static bool resource(Source *s, uint32_t value, uint32_t *out) {
    CompilerWorkspace *c = s->compiler;
    uint32_t depth = c->walk_count;
    for (;;) {
        if (!charge(s)) return false;
        if (value != 0u && value <= c->instruction_count) {
            const IrInstruction *ir = &c->instructions[value - 1u];
            if (ir->opcode == IR_HANDLE && ir->operands.scalar.binding < c->resource_count) {
                *out = ir->operands.scalar.binding;
                return true;
            }
            break;
        }
        if (depth <= 1u || value <= c->limits.instructions) break;
        const FunctionRecord *function = &c->functions[c->walk[depth - 1u].object];
        const uint32_t formal = value - c->limits.instructions - 1u;
        if (formal < function->formal_start || formal - function->formal_start >= function->formal_count) break;
        const IrInstruction *call = &c->instructions[c->walk[depth - 1u].parent];
        value = c->typed_operands[call->operands.scalar.left + formal - function->formal_start];
        --depth;
    }
    return source_error(s, DOE_COMPILER_INPUT_ERROR, s->length,
        "sampling handle lacks a validated resource or call-site formal identity");
}
static bool pair(Source *s, uint32_t entry, const IrInstruction *ir) {
    CompilerWorkspace *c = s->compiler;
    SamplingContext *context = sampling_context(s);
    uint32_t texture, sampler;
    if (!resource(s, c->typed_operands[ir->operands.composite.first], &texture) ||
        !resource(s, c->typed_operands[ir->operands.composite.first + 1u], &sampler)) return false;
    for (size_t i = 0u; i < context->count; ++i) {
        if (!charge(s)) return false;
        const DoeCompilerSamplingPairV16 *p = &context->staging[i];
        if (p->entry_index == entry && p->texture_resource_index == texture &&
            p->sampler_resource_index == sampler) return true;
    }
    if (context->count == context->capacity)
        return source_pool_error(s, DOE_COMPILER_POOL_SAMPLING_PAIRS, context->count + 1u,
            ir->source_offset, "sampling relationship staging capacity exceeded");
    context->staging[context->count++] = (DoeCompilerSamplingPairV16){entry,texture,sampler,0u};
    return true;
}
bool sampling_prepare(Source *s) {
    SamplingContext *context = sampling_context(s);
    if (context == NULL) return true;
    CompilerWorkspace *c = s->compiler;
    context->count = 0u;
    c->walk_count = 0u;
    for (uint32_t entry = 0u; entry < c->entry_count; ++entry) {
        if (!push(s, c->entries[entry].function, NO_ID)) return false;
        while (c->walk_count != 0u) {
            if (!charge(s)) return false;
            WalkItem *frame = &c->walk[c->walk_count - 1u];
            if (frame->cursor == c->functions[frame->object].instruction_end) {
                --c->walk_count;
                continue;
            }
            const uint32_t index = frame->cursor++;
            const IrInstruction *ir = &c->instructions[index];
            if (ir->opcode == IR_CALL) {
                if (!push(s, ir->operands.scalar.binding, index)) return false;
            } else if (ir->opcode == IR_IMAGE && image_sampling((ImageOperation)ir->operands.composite.kind)) {
                if (!pair(s, entry, ir)) return false;
            }
        }
    }
    return true;
}
