/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

static doe_resource_status active(doe_resource_compute_pass *pass)
{
    if (!pass) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (pass->encoder->status != DOE_RESOURCE_OK) return pass->encoder->status;
    if (pass->ended || pass->encoder->active_pass != pass)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    if (!resource_alive(&pass->base)) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_UNAVAILABLE);
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_begin_compute(doe_resource_encoder *encoder,
    doe_resource_compute_pass **out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_compute_pass *pass = resource_allocate(&encoder->base.config,
        sizeof(*pass), _Alignof(doe_resource_compute_pass));
    if (!pass) return resource_encoder_fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    memset(pass, 0, sizeof(*pass));
    pass->encoder = encoder;
    doe_resource_encoder_retain(encoder);
    resource_init(&pass->base, &encoder->base.config, (doe_resource_native){0});
    encoder->active_pass = pass;
    *out = pass;
    return DOE_RESOURCE_OK;
}

void doe_resource_compute_pass_retain(doe_resource_compute_pass *pass)
{
    if (pass) resource_retain(&pass->base);
}

void doe_resource_compute_pass_release(doe_resource_compute_pass *pass)
{
    if (!pass || !resource_last_reference(&pass->base)) return;
    doe_resource_encoder *encoder = pass->encoder;
    if (!pass->ended) {
        resource_encoder_fail(encoder, DOE_RESOURCE_STATE);
        if (encoder->active_pass == pass) encoder->active_pass = NULL;
    }
    doe_resource_pipeline_release(pass->pipeline);
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; i++) doe_resource_bind_group_release(pass->groups[i]);
    resource_finalize(&pass->base, sizeof(*pass), _Alignof(doe_resource_compute_pass));
    doe_resource_encoder_release(encoder);
}

doe_resource_status doe_resource_compute_pass_set_pipeline(doe_resource_compute_pass *pass,
    doe_resource_pipeline *pipeline)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!pipeline) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    if (pipeline->base.config.device.identity != pass->base.config.device.identity)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    doe_resource_pipeline_retain(pipeline);
    doe_resource_pipeline_release(pass->pipeline);
    pass->pipeline = pipeline;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_compute_pass_set_group(doe_resource_compute_pass *pass,
    uint32_t index, const doe_resource_dispatch_group *group)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (index >= DOE_RESOURCE_MAX_GROUPS) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    if (!group || (!group->group && !group->dynamic_offset_count)) {
        doe_resource_bind_group_release(pass->groups[index]);
        pass->groups[index] = NULL;
        pass->offsets.counts[index] = 0;
        return DOE_RESOURCE_OK;
    }
    doe_resource_binding resolved[DOE_RESOURCE_MAX_BINDINGS];
    status = doe_resource_bind_group_resolve(group->group, group->dynamic_offsets,
        group->dynamic_offset_count, resolved, DOE_RESOURCE_MAX_BINDINGS);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
    if (group->group->base.config.device.identity != pass->base.config.device.identity)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    doe_resource_bind_group_retain(group->group);
    doe_resource_bind_group_release(pass->groups[index]);
    pass->groups[index] = group->group;
    pass->offsets.counts[index] = group->dynamic_offset_count;
    if (group->dynamic_offset_count) memcpy(pass->offsets.values[index], group->dynamic_offsets,
        group->dynamic_offset_count * sizeof(uint32_t));
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_compute_pass_dispatch(doe_resource_compute_pass *pass,
    uint32_t x, uint32_t y, uint32_t z)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!pass->pipeline) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    size_t count = pass->pipeline->layout->count;
    doe_resource_dispatch_group groups[DOE_RESOURCE_MAX_GROUPS];
    for (size_t i = 0; i < count; i++)
        groups[i] = (doe_resource_dispatch_group){pass->groups[i], pass->offsets.values[i], pass->offsets.counts[i]};
    return resource_record_dispatch(pass->encoder, pass->pipeline, groups, count, x, y, z);
}

doe_resource_status doe_resource_compute_pass_dispatch_indirect(doe_resource_compute_pass *pass,
    doe_resource_buffer *arguments, uint64_t offset)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!pass->pipeline) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    size_t count = pass->pipeline->layout->count;
    doe_resource_dispatch_group groups[DOE_RESOURCE_MAX_GROUPS];
    for (size_t i = 0; i < count; i++)
        groups[i] = (doe_resource_dispatch_group){pass->groups[i], pass->offsets.values[i], pass->offsets.counts[i]};
    return resource_record_dispatch_indirect(pass->encoder, pass->pipeline, groups, count, arguments, offset);
}

doe_resource_status doe_resource_compute_pass_end(doe_resource_compute_pass *pass)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    pass->ended = true;
    pass->encoder->active_pass = NULL;
    return DOE_RESOURCE_OK;
}
