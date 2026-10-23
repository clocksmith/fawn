/* SPDX-License-Identifier: MIT */
#include "internal.h"

static bool same_register(uint32_t reg_a, uint32_t space_a, doe_dx_binding_access access_a,
    uint32_t reg_b, uint32_t space_b, doe_dx_binding_access access_b) {
    return reg_a == reg_b && space_a == space_b && access_a == access_b;
}

static doe_dx_status validate_layout(const doe_dx_resource_pipeline_desc *desc) {
    const doe_dx_pipeline_desc *buffers = &desc->buffers;
    if (!buffers->bytecode || !buffers->bytecode_size ||
        (buffers->binding_count && !buffers->bindings) ||
        (desc->texture_count && !desc->textures) || (desc->sampler_count && !desc->samplers))
        return DOE_DX_INVALID_ARGUMENT;
    if (desc->texture_count > DOE_DX_MAX_BINDINGS || desc->sampler_count > DOE_DX_MAX_BINDINGS ||
        buffers->binding_count > DOE_DX_MAX_BINDINGS - desc->texture_count)
        return DOE_DX_RANGE;
    for (size_t index = 0; index < buffers->binding_count; index++) {
        const doe_dx_binding_layout *binding = &buffers->bindings[index];
        if (binding->stride < 4 || binding->stride > 2048 || binding->stride % 4 ||
            (binding->access != DOE_DX_READ_ONLY && binding->access != DOE_DX_READ_WRITE))
            return DOE_DX_INVALID_ARGUMENT;
        for (size_t prior = 0; prior < index; prior++) {
            const doe_dx_binding_layout *other = &buffers->bindings[prior];
            if (same_register(binding->shader_register, binding->register_space, binding->access,
                other->shader_register, other->register_space, other->access)) return DOE_DX_INVALID_ARGUMENT;
        }
    }
    for (size_t index = 0; index < desc->texture_count; index++) {
        const doe_dx_texture_binding_layout *binding = &desc->textures[index];
        if (!doe_dx_format_valid(binding->format) ||
            (binding->access != DOE_DX_READ_ONLY && binding->access != DOE_DX_READ_WRITE))
            return DOE_DX_INVALID_ARGUMENT;
        for (size_t prior = 0; prior < index; prior++) {
            const doe_dx_texture_binding_layout *other = &desc->textures[prior];
            if (same_register(binding->shader_register, binding->register_space, binding->access,
                other->shader_register, other->register_space, other->access)) return DOE_DX_INVALID_ARGUMENT;
        }
        for (size_t buffer = 0; buffer < buffers->binding_count; buffer++) {
            const doe_dx_binding_layout *other = &buffers->bindings[buffer];
            if (same_register(binding->shader_register, binding->register_space, binding->access,
                other->shader_register, other->register_space, other->access)) return DOE_DX_INVALID_ARGUMENT;
        }
    }
    for (size_t index = 0; index < desc->sampler_count; index++) {
        const doe_dx_static_sampler *sampler = &desc->samplers[index];
        if ((sampler->filter != DOE_DX_NEAREST && sampler->filter != DOE_DX_LINEAR) ||
            sampler->address_u < DOE_DX_CLAMP || sampler->address_u > DOE_DX_MIRROR ||
            sampler->address_v < DOE_DX_CLAMP || sampler->address_v > DOE_DX_MIRROR)
            return DOE_DX_INVALID_ARGUMENT;
        for (size_t prior = 0; prior < index; prior++)
            if (sampler->shader_register == desc->samplers[prior].shader_register &&
                sampler->register_space == desc->samplers[prior].register_space)
                return DOE_DX_INVALID_ARGUMENT;
    }
    return DOE_DX_OK;
}

static doe_dx_status create_pipeline(doe_dx_context *context,
    const doe_dx_resource_pipeline_desc *desc, const doe_dx_graphics_pipeline_desc *graphics,
    doe_dx_pipeline **out, doe_dx_error *error) {
    if (!context || !desc || !out)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "pipeline.create", error);
    doe_dx_status status = validate_layout(desc);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "pipeline.layout", error);
    if (context->objects == SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE, 0, "pipeline.create", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "pipeline.create", error);
    doe_dx_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "pipeline.allocate", error);
    if (graphics) status = doe_dx_native_graphics_create(context->native, graphics, &pipeline->native, error);
    else status = doe_dx_native_pipeline_create(context->native, desc, &pipeline->native, error);
    if (status != DOE_DX_OK) {
        free(pipeline);
        return doe_dx_context_result(context, status, error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->graphics = graphics != NULL;
    if (graphics) pipeline->color_format = graphics->color_format;
    pipeline->binding_count = desc->buffers.binding_count;
    pipeline->texture_count = desc->texture_count;
    if (pipeline->binding_count)
        memcpy(pipeline->bindings, desc->buffers.bindings,
            pipeline->binding_count * sizeof(*pipeline->bindings));
    if (pipeline->texture_count)
        memcpy(pipeline->textures, desc->textures,
            pipeline->texture_count * sizeof(*pipeline->textures));
    context->objects++;
    *out = pipeline;
    return doe_dx_fail(DOE_DX_OK, 0, "pipeline.create", error);
}

doe_dx_status doe_dx_pipeline_create_resources(doe_dx_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error) {
    return create_pipeline(context, desc, NULL, out, error);
}

doe_dx_status doe_dx_pipeline_create_graphics(doe_dx_context *context,
    const doe_dx_graphics_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error) {
    if (!desc || !desc->vertex_bytecode || !desc->vertex_bytecode_size)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "graphics.create", error);
    if (desc->color_format != DOE_DX_RGBA8_UNORM)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "graphics.format", error);
    return create_pipeline(context, &desc->fragment, desc, out, error);
}

doe_dx_status doe_dx_pipeline_create(doe_dx_context *context,
    const doe_dx_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error) {
    if (!desc) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "pipeline.create", error);
    doe_dx_resource_pipeline_desc resources = {0};
    resources.buffers = *desc;
    return doe_dx_pipeline_create_resources(context, &resources, out, error);
}

doe_dx_status doe_dx_pipeline_retain(doe_dx_pipeline *pipeline) {
    if (!pipeline) return DOE_DX_INVALID_ARGUMENT;
    if (pipeline->references == UINT32_MAX) return DOE_DX_RANGE;
    pipeline->references++;
    return DOE_DX_OK;
}

void doe_dx_pipeline_release(doe_dx_pipeline *pipeline) {
    if (!pipeline) return;
    pipeline->references--;
    if (pipeline->references) return;
    doe_dx_native_pipeline_destroy(pipeline->native);
    pipeline->context->objects--;
    free(pipeline);
}
