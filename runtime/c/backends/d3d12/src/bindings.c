/* SPDX-License-Identifier: MIT */
#include "internal.h"

static bool overlapping(const doe_dx_buffer_binding *a, const doe_dx_buffer_binding *b) {
    return a->offset <= b->offset ? b->offset - a->offset < a->size :
        a->offset - b->offset < b->size;
}

/* Shared graphics/compute resource rules: validation only, no publication. */
doe_dx_status doe_dx_prepare_binding_plan(doe_dx_commands *commands,const doe_dx_binding_plan *plan,
    const doe_dx_dynamic_resources *values, doe_dx_native_resources *native,
    doe_dx_reservation *needed, doe_dx_error *error) {
    if (!plan || !values)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.resources", error);
    const doe_dx_compute_resources *resources = &values->resources;
    doe_dx_status status;
    const doe_dx_buffer_binding *bindings = resources->buffers;
    size_t binding_count = resources->buffer_count;
    if ((binding_count && !bindings) || (resources->texture_count && !resources->textures))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.bindings", error);
    if (binding_count != plan->binding_count || resources->texture_count != plan->texture_count)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.bindings.bindings", error);
    if (values->sampler_count != plan->sampler_count || (values->sampler_count && !values->samplers))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.samplers",error);
    for (size_t index = 0; index < values->sampler_count; ++index) {
        status = doe_dx_sampler_validate(&values->samplers[index]);
        if (status != DOE_DX_OK) return doe_dx_fail(status,0,"commands.sampler.values",error);
        if ((values->samplers[index].compare != DOE_DX_SAMPLER_NO_COMPARE) != plan->samplers[index].comparison)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.sampler.comparison",error);
        native->samplers[index] = values->samplers[index];
    }
    native->sampler_count = values->sampler_count;
    native->buffer_count = binding_count;
    native->texture_count = resources->texture_count;
    *needed = (doe_dx_reservation){0};
    for (size_t index = 0; index < binding_count; index++) {
        const doe_dx_buffer_binding *binding = &bindings[index];
        doe_dx_buffer *buffer = binding->buffer;
        status = doe_dx_buffer_preflight(commands, buffer, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.bindings.buffer", error);
        if (buffer->memory != DOE_DX_DEFAULT)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.bindings.heap", error);
        if (!binding->size || !doe_dx_range(buffer->size, binding->offset, binding->size))
            return doe_dx_fail(DOE_DX_RANGE, 0, "commands.bindings.range", error);
        const doe_dx_binding_layout *layout = &plan->bindings[index];
        if (doe_dx_buffer_descriptor_kind(layout) == DOE_DX_DESCRIPTOR_CBV) {
            if (binding->offset % commands->context->caps.limits.uniform_offset_alignment ||
                binding->size % commands->context->caps.limits.uniform_offset_alignment)
                return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.bindings.uniform_alignment", error);
            if (binding->size > commands->context->caps.limits.max_uniform_bytes)
                return doe_dx_fail(DOE_DX_RANGE, 0, "commands.bindings.uniform_size", error);
        } else {
            uint32_t stride = layout->stride;
            if (binding->offset % stride || binding->size % stride)
                return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "commands.bindings.stride", error);
            if (binding->size / stride > UINT32_MAX)
                return doe_dx_fail(DOE_DX_RANGE, 0, "commands.bindings.elements", error);
        }
        bool repeated = false;
        for (size_t prior = 0; prior < index; prior++) {
            if (bindings[prior].buffer != buffer) continue;
            repeated = true;
            bool writable = doe_dx_buffer_descriptor_kind(layout) == DOE_DX_DESCRIPTOR_UAV;
            bool other_writable = doe_dx_buffer_descriptor_kind(&plan->bindings[prior]) == DOE_DX_DESCRIPTOR_UAV;
            if (writable != other_writable || (writable && overlapping(binding, &bindings[prior])))
                return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.bindings.alias", error);
        }
        if (!repeated && !doe_dx_find_buffer(commands, buffer)) needed->buffers++;
        native->buffers[index] = (doe_dx_native_binding){buffer->native, binding->offset, binding->size};
    }
    for (size_t index = 0; index < resources->texture_count; index++) {
        doe_dx_texture *texture = resources->textures[index];
        const doe_dx_texture_binding_layout *layout = &plan->textures[index];
        const doe_dx_texture_contract *contract = &plan->texture_contracts[index];
        uint32_t usage = contract->storage || layout->access == DOE_DX_READ_WRITE ?
            DOE_DX_TEXTURE_STORAGE : DOE_DX_TEXTURE_SAMPLED;
        status = doe_dx_texture_preflight(commands, texture, usage, true);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.bindings.texture", error);
        doe_dx_texture_view_desc view;
        status=doe_dx_texture_view_validate(texture,values->texture_views ? &values->texture_views[index] : NULL,&view);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"commands.bindings.texture_view",error);
        if (view.dimension!=(contract->dimension ? contract->dimension : 2u) ||
            ((contract->storage || layout->access==DOE_DX_READ_WRITE) && view.mip_count!=1))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.bindings.texture_dimension",error);
        bool multisampled=contract->sample_class==DOE_DX_SAMPLE_FLOAT_MS || contract->sample_class==DOE_DX_SAMPLE_UINT_MS ||
            contract->sample_class==DOE_DX_SAMPLE_SINT_MS || contract->sample_class==DOE_DX_SAMPLE_DEPTH_MS;
        if (multisampled!=(texture->samples>1))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.bindings.texture_samples",error);
        doe_dx_format_info info;
        if (!doe_dx_format_properties(view.format,&info)) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"commands.texture_format",error);
        if (view.aspect==2) info.kind=2;
        bool matches = contract->sample_class == DOE_DX_SAMPLE_EXACT ? view.format == layout->format :
            (contract->sample_class == DOE_DX_SAMPLE_FLOAT || contract->sample_class == DOE_DX_SAMPLE_FLOAT_MS) ? info.kind==1 :
            (contract->sample_class == DOE_DX_SAMPLE_UINT || contract->sample_class == DOE_DX_SAMPLE_UINT_MS) ? info.kind==2 :
            (contract->sample_class == DOE_DX_SAMPLE_SINT || contract->sample_class == DOE_DX_SAMPLE_SINT_MS) ? info.kind==3 : info.kind>=4;
        if (!matches) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.bindings.texture_format_class",error);
        native->texture_views[index]=view;
        if (!values->texture_views && !contract->dimension) native->texture_views[index].dimension=0;
        bool repeated = false;
        for (size_t prior = 0; prior < index; prior++) {
            if (resources->textures[prior] != texture) continue;
            repeated = true;
            if (layout->access != DOE_DX_READ_ONLY || plan->textures[prior].access != DOE_DX_READ_ONLY)
                return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"commands.bindings.texture_alias",error);
        }
        if (!repeated && !doe_dx_find_texture(commands, texture)) needed->textures++;
        native->textures[index] = texture->native;
    }
    return DOE_DX_OK;
}

doe_dx_status doe_dx_prepare_dynamic_resources(doe_dx_commands *commands,doe_dx_pipeline *pipeline,
    const doe_dx_dynamic_resources *values,doe_dx_native_resources *native,
    doe_dx_reservation *needed,doe_dx_error *error) {
    if (!pipeline || pipeline->render) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.pipeline",error);
    if (pipeline->context != commands->context) return doe_dx_fail(DOE_DX_DEVICE_MISMATCH,0,"commands.pipeline",error);
    if (pipeline->references == UINT32_MAX) return doe_dx_fail(DOE_DX_RANGE,0,"commands.pipeline_references",error);
    const doe_dx_dynamic_pipeline_desc desc = {.resources={.buffers={.bindings=pipeline->bindings,
        .binding_count=pipeline->binding_count},.textures=pipeline->textures,.texture_count=pipeline->texture_count},
        .texture_contracts=pipeline->texture_contracts,.samplers=pipeline->samplers,.sampler_count=pipeline->sampler_count};
    doe_dx_binding_plan plan;
    doe_dx_binding_plan_copy(&plan,&desc);
    doe_dx_status status = doe_dx_prepare_binding_plan(commands,&plan,values,native,needed,error);
    if (status == DOE_DX_OK) needed->pipelines=1;
    return status;
}

doe_dx_status doe_dx_prepare_resources(doe_dx_commands *commands,doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources,doe_dx_native_resources *native,
    doe_dx_reservation *needed,doe_dx_error *error) {
    if (!resources) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"commands.resources",error);
    const doe_dx_dynamic_resources values = {.resources=*resources};
    return doe_dx_prepare_dynamic_resources(commands,pipeline,&values,native,needed,error);
}

void doe_dx_use_resources(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources) {
    for (size_t index = 0; index < resources->buffer_count; index++)
        doe_dx_use_buffer(commands, resources->buffers[index].buffer);
    for (size_t index = 0; index < resources->texture_count; index++)
        doe_dx_use_texture(commands, resources->textures[index]);
    if (commands->pipeline_count >= commands->pipeline_capacity) abort();
    pipeline->references++;
    commands->pipelines[commands->pipeline_count++] = pipeline;
}

/* Cross-stage aliases use the same allocation/range classes as one stage. */
doe_dx_status doe_dx_stage_aliases(const doe_dx_binding_plan *a,const doe_dx_dynamic_resources *av,
    const doe_dx_binding_plan *b,const doe_dx_dynamic_resources *bv) {
    for (size_t i=0;i<a->binding_count;++i) for (size_t j=0;j<b->binding_count;++j) {
        const doe_dx_buffer_binding *x=&av->resources.buffers[i],*y=&bv->resources.buffers[j];
        if (x->buffer != y->buffer) continue;
        bool x_write=doe_dx_buffer_descriptor_kind(&a->bindings[i])==DOE_DX_DESCRIPTOR_UAV;
        bool y_write=doe_dx_buffer_descriptor_kind(&b->bindings[j])==DOE_DX_DESCRIPTOR_UAV;
        if (x_write != y_write || (x_write && overlapping(x,y))) return DOE_DX_UNSUPPORTED;
    }
    for (size_t i=0;i<a->texture_count;++i) for (size_t j=0;j<b->texture_count;++j)
        if (av->resources.textures[i]==bv->resources.textures[j] &&
            (a->textures[i].access!=DOE_DX_READ_ONLY || b->textures[j].access!=DOE_DX_READ_ONLY))
            return DOE_DX_UNSUPPORTED;
    return DOE_DX_OK;
}
