/* SPDX-License-Identifier: MIT */
#include "internal.h"

static doe_dx_descriptor_kind texture_kind(doe_dx_binding_access access) {
    if (access == DOE_DX_READ_ONLY) return DOE_DX_DESCRIPTOR_SRV;
    if (access == DOE_DX_READ_WRITE) return DOE_DX_DESCRIPTOR_UAV;
    return DOE_DX_DESCRIPTOR_INVALID;
}

doe_dx_descriptor_kind doe_dx_buffer_descriptor_kind(const doe_dx_binding_layout *layout) {
    if (layout->view == DOE_DX_STRUCTURED || layout->view == DOE_DX_RAW) return texture_kind(layout->access);
    if (layout->view == DOE_DX_UNIFORM && layout->access == DOE_DX_READ_ONLY)
        return DOE_DX_DESCRIPTOR_CBV;
    return DOE_DX_DESCRIPTOR_INVALID;
}

static bool same_register(uint32_t reg_a, uint32_t space_a, doe_dx_descriptor_kind kind_a,
    uint32_t reg_b, uint32_t space_b, doe_dx_descriptor_kind kind_b) {
    return reg_a == reg_b && space_a == space_b && kind_a == kind_b;
}

doe_dx_status doe_dx_validate_layout(const doe_dx_context *context, const doe_dx_resource_pipeline_desc *desc,
    const doe_dx_dynamic_pipeline_desc *dynamic) {
    const doe_dx_pipeline_desc *buffers = &desc->buffers;
    if (!buffers->bytecode || !buffers->bytecode_size ||
        (buffers->binding_count && !buffers->bindings) ||
        (desc->texture_count && !desc->textures) || (desc->sampler_count && !desc->samplers))
        return DOE_DX_INVALID_ARGUMENT;
    if (desc->texture_count > DOE_DX_MAX_BINDINGS || desc->sampler_count > DOE_DX_MAX_STATIC_SAMPLERS ||
        buffers->binding_count > DOE_DX_MAX_BINDINGS - desc->texture_count)
        return DOE_DX_RANGE;
    if (dynamic) {
        if (dynamic->logical_size_count>16384 ||
            (dynamic->logical_size_count && !dynamic->logical_size_indices)) return DOE_DX_INVALID_ARGUMENT;
        uint32_t bytes=dynamic->immediate_bytes;
        if (bytes>64 || bytes%4) return DOE_DX_INVALID_ARGUMENT;
        uint64_t allowed=bytes==64 ? UINT64_MAX : ((UINT64_C(1)<<bytes)-1);
        if (dynamic->immediate_required_bytes & ~allowed) return DOE_DX_INVALID_ARGUMENT;
    }
    size_t runtime_samplers = dynamic ? dynamic->sampler_count : 0;
    if (runtime_samplers > DOE_DX_MAX_DYNAMIC_SAMPLERS ||
        runtime_samplers > DOE_DX_MAX_DYNAMIC_SAMPLERS - desc->sampler_count) return DOE_DX_RANGE;
    if (runtime_samplers && !dynamic->samplers) return DOE_DX_INVALID_ARGUMENT;
    const doe_dx_limits *limits = &context->caps.limits;
    if (buffers->binding_count + desc->texture_count > limits->max_descriptors ||
        desc->sampler_count > limits->max_static_samplers) return DOE_DX_RANGE;
    uint32_t cbvs = dynamic ? (dynamic->immediate_bytes ? 1u : 0u)+(dynamic->logical_size_count ? 1u : 0u) : 0u;
    uint32_t srvs = 0, uavs = 0;
    for (size_t index = 0; index < buffers->binding_count; index++) {
        const doe_dx_binding_layout *binding = &buffers->bindings[index];
        doe_dx_descriptor_kind kind = doe_dx_buffer_descriptor_kind(binding);
        if (dynamic && dynamic->immediate_bytes && kind==DOE_DX_DESCRIPTOR_CBV &&
            binding->shader_register==1 && binding->register_space==7) return DOE_DX_INVALID_ARGUMENT;
        if (dynamic && dynamic->logical_size_count && kind==DOE_DX_DESCRIPTOR_CBV &&
            binding->shader_register==2 && binding->register_space==7) return DOE_DX_INVALID_ARGUMENT;
        if (binding->view==DOE_DX_RAW && (!dynamic || !dynamic->logical_size_count ||
            dynamic->logical_size_indices[index]>=dynamic->logical_size_count)) return DOE_DX_INVALID_ARGUMENT;
        switch (kind) {
        case DOE_DX_DESCRIPTOR_CBV: ++cbvs; break;
        case DOE_DX_DESCRIPTOR_SRV: ++srvs; break;
        case DOE_DX_DESCRIPTOR_UAV: ++uavs; break;
        case DOE_DX_DESCRIPTOR_INVALID: return DOE_DX_INVALID_ARGUMENT;
        }
        if ((kind == DOE_DX_DESCRIPTOR_CBV || binding->view==DOE_DX_RAW ? binding->stride != 0 :
                binding->stride < 4 || binding->stride > 2048 || binding->stride % 4))
            return DOE_DX_INVALID_ARGUMENT;
        for (size_t prior = 0; prior < index; prior++) {
            const doe_dx_binding_layout *other = &buffers->bindings[prior];
            if (binding->view==DOE_DX_RAW && other->view==DOE_DX_RAW &&
                dynamic->logical_size_indices[index]==dynamic->logical_size_indices[prior]) return DOE_DX_INVALID_ARGUMENT;
            if (same_register(binding->shader_register, binding->register_space, kind,
                other->shader_register, other->register_space, doe_dx_buffer_descriptor_kind(other))) return DOE_DX_INVALID_ARGUMENT;
        }
    }
    for (size_t index = 0; index < desc->texture_count; index++) {
        const doe_dx_texture_binding_layout *binding = &desc->textures[index];
        doe_dx_texture_contract contract = dynamic && dynamic->texture_contracts ?
            dynamic->texture_contracts[index] : (doe_dx_texture_contract){0};
        if (contract.sample_class == DOE_DX_SAMPLE_EXACT && doe_dx_format_depth(binding->format))
            return DOE_DX_UNSUPPORTED;
        if (contract.sample_class < DOE_DX_SAMPLE_EXACT || contract.sample_class > DOE_DX_SAMPLE_DEPTH_MS ||
            ((binding->access != DOE_DX_READ_ONLY || contract.storage) && contract.sample_class != DOE_DX_SAMPLE_EXACT) ||
            (contract.sample_class == DOE_DX_SAMPLE_EXACT && !doe_dx_format_valid(binding->format)) ||
            (binding->access != DOE_DX_READ_ONLY && binding->access != DOE_DX_READ_WRITE))
            return DOE_DX_INVALID_ARGUMENT;
        if (contract.dimension>6) return DOE_DX_INVALID_ARGUMENT;
        if (binding->access == DOE_DX_READ_ONLY) ++srvs;
        else ++uavs;
        for (size_t prior = 0; prior < index; prior++) {
            const doe_dx_texture_binding_layout *other = &desc->textures[prior];
            if (same_register(binding->shader_register, binding->register_space, texture_kind(binding->access),
                other->shader_register, other->register_space, texture_kind(other->access))) return DOE_DX_INVALID_ARGUMENT;
        }
        for (size_t buffer = 0; buffer < buffers->binding_count; buffer++) {
            const doe_dx_binding_layout *other = &buffers->bindings[buffer];
            if (same_register(binding->shader_register, binding->register_space, texture_kind(binding->access),
                other->shader_register, other->register_space, doe_dx_buffer_descriptor_kind(other))) return DOE_DX_INVALID_ARGUMENT;
        }
    }
    if (cbvs > limits->max_cbvs || srvs > limits->max_srvs || uavs > limits->max_uavs)
        return DOE_DX_RANGE;
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
    for (size_t index = 0; index < runtime_samplers; ++index) {
        const doe_dx_sampler_layout *sampler = &dynamic->samplers[index];
        for (size_t prior = 0; prior < index; ++prior)
            if (sampler->shader_register == dynamic->samplers[prior].shader_register &&
                sampler->register_space == dynamic->samplers[prior].register_space) return DOE_DX_INVALID_ARGUMENT;
        for (size_t prior = 0; prior < desc->sampler_count; ++prior)
            if (sampler->shader_register == desc->samplers[prior].shader_register &&
                sampler->register_space == desc->samplers[prior].register_space) return DOE_DX_INVALID_ARGUMENT;
    }
    return DOE_DX_OK;
}

static doe_dx_status create_pipeline(doe_dx_context *context,
    const doe_dx_resource_pipeline_desc *desc, const doe_dx_dynamic_pipeline_desc *dynamic,
    const doe_dx_mesh_pipeline_desc *mesh, doe_dx_pipeline **out, doe_dx_error *error) {
    if (!context || !desc || !out)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "pipeline.create", error);
    uint32_t ends[DOE_DX_MAX_VERTEX_BUFFERS] = {0};
    doe_dx_status status = mesh ? doe_dx_validate_mesh(mesh, ends) : DOE_DX_OK;
    if (status == DOE_DX_OK) status = doe_dx_validate_layout(context, desc, dynamic);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "pipeline.layout", error);
    if (context->objects == SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE, 0, "pipeline.create", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "pipeline.create", error);
    doe_dx_pipeline *pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "pipeline.allocate", error);
    if (mesh) status = doe_dx_native_graphics_create(context->native, mesh, &pipeline->native, error);
    else if (dynamic) status = doe_dx_native_pipeline_create_dynamic(context->native, dynamic, &pipeline->native, error);
    else status = doe_dx_native_pipeline_create(context->native, desc, &pipeline->native, error);
    if (status != DOE_DX_OK) {
        free(pipeline);
        return doe_dx_context_result(context, status, error);
    }
    pipeline->context = context;
    pipeline->references = 1;
    pipeline->graphics = mesh != NULL;
    if (mesh) {
        pipeline->color_format = mesh->graphics.color_format;
        pipeline->depth_enabled = mesh->depth_enabled;
        pipeline->vertex_buffer_count = mesh->vertex_buffer_count;
        for (size_t index = 0; index < mesh->vertex_buffer_count; index++) {
            pipeline->vertex_buffers[index] = mesh->vertex_buffers[index];
            pipeline->vertex_ends[index] = ends[index];
        }
    }
    pipeline->binding_count = desc->buffers.binding_count;
    pipeline->texture_count = desc->texture_count;
    if (pipeline->binding_count)
        memcpy(pipeline->bindings, desc->buffers.bindings,
            pipeline->binding_count * sizeof(*pipeline->bindings));
    if (pipeline->texture_count)
        memcpy(pipeline->textures, desc->textures,
            pipeline->texture_count * sizeof(*pipeline->textures));
    if (dynamic) {
        pipeline->logical_size_count=dynamic->logical_size_count;
        if (dynamic->logical_size_count) memcpy(pipeline->logical_size_indices,dynamic->logical_size_indices,
            pipeline->binding_count*sizeof(*pipeline->logical_size_indices));
        pipeline->immediate_bytes=dynamic->immediate_bytes;
        pipeline->immediate_required_bytes=dynamic->immediate_required_bytes;
        if (dynamic->texture_contracts && pipeline->texture_count)
            memcpy(pipeline->texture_contracts,dynamic->texture_contracts,
                pipeline->texture_count * sizeof(*pipeline->texture_contracts));
        pipeline->sampler_count = dynamic->sampler_count;
        if (pipeline->sampler_count)
            memcpy(pipeline->samplers,dynamic->samplers,pipeline->sampler_count * sizeof(*pipeline->samplers));
    }
    context->objects++;
    *out = pipeline;
    return doe_dx_fail(DOE_DX_OK, 0, "pipeline.create", error);
}

doe_dx_status doe_dx_pipeline_create_resources(doe_dx_context *context,
    const doe_dx_resource_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error) {
    return create_pipeline(context, desc, NULL, NULL, out, error);
}

doe_dx_status doe_dx_pipeline_create_dynamic(doe_dx_context *context,
    const doe_dx_dynamic_pipeline_desc *desc,doe_dx_pipeline **out,doe_dx_error *error) {
    if (!desc) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"pipeline.dynamic",error);
    return create_pipeline(context,&desc->resources,desc,NULL,out,error);
}

doe_dx_status doe_dx_pipeline_create_graphics(doe_dx_context *context,
    const doe_dx_graphics_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error) {
    if (!desc) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "graphics.create", error);
    doe_dx_mesh_pipeline_desc mesh = {0};
    mesh.graphics = *desc;
    return doe_dx_pipeline_create_mesh(context, &mesh, out, error);
}

doe_dx_status doe_dx_pipeline_create_mesh(doe_dx_context *context,
    const doe_dx_mesh_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error) {
    if (!desc) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "mesh.create", error);
    return create_pipeline(context, &desc->graphics.fragment, NULL, desc, out, error);
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
    free(pipeline->render);
    free(pipeline);
}
