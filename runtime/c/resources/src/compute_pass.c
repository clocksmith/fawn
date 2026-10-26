/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

static doe_resource_status active(doe_resource_compute_pass *pass)
{
    if (!pass) return DOE_RESOURCE_INVALID_ARGUMENT;
    /* A command on an ended pass is reported at the call; the parent stays valid. */
    if (pass->ended) return DOE_RESOURCE_STATE;
    if (pass->encoder->status != DOE_RESOURCE_OK) return pass->encoder->status;
    if (pass->encoder->active_pass != pass) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    if (!resource_alive(&pass->base)) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_UNAVAILABLE);
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_encoder_begin_compute(doe_resource_encoder *encoder,
    doe_resource_compute_pass **out)
{
    return doe_resource_encoder_begin_compute_with_timestamps(encoder, NULL, out);
}

doe_resource_status doe_resource_encoder_begin_compute_with_timestamps(doe_resource_encoder *encoder,
    const doe_resource_pass_timestamps *timestamps, doe_resource_compute_pass **out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_pass_timestamps_validate(encoder->base.config.device.identity, timestamps);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    doe_resource_compute_pass *pass = resource_allocate(&encoder->base.config,
        sizeof(*pass), _Alignof(doe_resource_compute_pass));
    if (!pass) return resource_encoder_fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    memset(pass, 0, sizeof(*pass));
    pass->encoder = encoder;
    doe_resource_encoder_retain(encoder);
    resource_init(&pass->base, &encoder->base.config, (doe_resource_native){0});
    encoder->active_pass = pass;
    if (timestamps) {
        pass->timestamps = *timestamps;
        doe_resource_query_set_retain(timestamps->set);
        if (timestamps->begin_index != UINT32_MAX) {
            status = resource_record_timestamp(encoder, timestamps->set, timestamps->begin_index, DOE_TIMESTAMP_PASS_BEGIN);
            if (status != DOE_RESOURCE_OK) {
                doe_resource_compute_pass_release(pass);
                return status;
            }
        }
    }
    *out = pass;
    return DOE_RESOURCE_OK;
}

doe_resource_compute_pass *doe_resource_compute_pass_rejected(doe_resource_encoder *encoder)
{
    if (!encoder || (encoder->status == DOE_RESOURCE_OK && !encoder->finished)) return NULL;
    doe_resource_compute_pass *pass = resource_allocate(&encoder->base.config,
        sizeof(*pass), _Alignof(doe_resource_compute_pass));
    if (!pass) return NULL;
    memset(pass, 0, sizeof(*pass));
    pass->encoder = encoder;
    /* A pass rejected after Finish cannot record or reopen its parent. Mark it
     * ended so commands/release preserve the parent's first recording status. */
    pass->ended = encoder->finished;
    doe_resource_encoder_retain(encoder);
    resource_init(&pass->base, &encoder->base.config, (doe_resource_native){0});
    /* begin() on an open encoder locks it even for an invalid pass; a locked or
     * ended encoder stays as it is. */
    if (!encoder->finished && !encoder->active_pass) encoder->active_pass = pass;
    return pass;
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
    doe_resource_query_set_release(pass->timestamps.set);
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

doe_resource_status doe_resource_compute_pass_set_immediates(doe_resource_compute_pass *pass,
    uint32_t offset, const void *data, size_t size)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    return resource_encoder_fail(pass->encoder,
        resource_immediates_update(&pass->base.config, &pass->immediates, offset, data, size));
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
    if (group->dynamic_offset_count > DOE_RESOURCE_MAX_DYNAMIC_OFFSETS)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_LAYOUT);
    status = resource_group_check(group->group, group->dynamic_offsets, group->dynamic_offset_count);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
    if (group->group->base.config.device.identity != pass->base.config.device.identity)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_DEVICE_MISMATCH);
    status = resource_encoder_reference_group(pass->encoder, group->group);
    if (status != DOE_RESOURCE_OK) return status;
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
    if ((pass->immediates.initialized_bytes & pass->pipeline->immediate_required_bytes) !=
        pass->pipeline->immediate_required_bytes) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    size_t count = pass->pipeline->layout->count;
    doe_resource_dispatch_group groups[DOE_RESOURCE_MAX_GROUPS];
    for (size_t i = 0; i < count; i++)
        groups[i] = (doe_resource_dispatch_group){pass->groups[i], pass->offsets.values[i], pass->offsets.counts[i]};
    status = resource_record_dispatch(pass->encoder, pass->pipeline, groups, count, x, y, z);
    if (status == DOE_RESOURCE_OK) pass->encoder->commands[pass->encoder->count - 1].value.dispatch.immediates = pass->immediates;
    return status;
}

doe_resource_status doe_resource_compute_pass_dispatch_indirect(doe_resource_compute_pass *pass,
    doe_resource_buffer *arguments, uint64_t offset)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!pass->pipeline) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    if ((pass->immediates.initialized_bytes & pass->pipeline->immediate_required_bytes) !=
        pass->pipeline->immediate_required_bytes) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    size_t count = pass->pipeline->layout->count;
    doe_resource_dispatch_group groups[DOE_RESOURCE_MAX_GROUPS];
    for (size_t i = 0; i < count; i++)
        groups[i] = (doe_resource_dispatch_group){pass->groups[i], pass->offsets.values[i], pass->offsets.counts[i]};
    status = resource_record_dispatch_indirect(pass->encoder, pass->pipeline, groups, count, arguments, offset);
    if (status == DOE_RESOURCE_OK) pass->encoder->commands[pass->encoder->count - 1].value.dispatch.immediates = pass->immediates;
    return status;
}

bool doe_resource_compute_pass_ended(const doe_resource_compute_pass *pass)
{
    return pass && pass->ended;
}

static doe_resource_status compute_pass_close(doe_resource_compute_pass *pass);
/* End closes an open pass even when it or its encoder is invalid. */
bool doe_resource_compute_pass_closed(const doe_resource_compute_pass *pass)
{
    return pass && (pass->ended || !pass->encoder->active_pass);
}

/* WebGPU end(): an ended pass or a parent encoder that is not locked reports at
 * the call and changes nothing. Otherwise the pass ends and its parent unlocks
 * (even when another pass locked it); the recorded effects need a valid pass. */
doe_resource_status doe_resource_compute_pass_end(doe_resource_compute_pass *pass)
{
    if (!pass) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (doe_resource_compute_pass_closed(pass)) return DOE_RESOURCE_STATE;
    doe_resource_status status = compute_pass_close(pass);
    pass->ended = true;
    pass->encoder->active_pass = NULL;
    return status;
}

static doe_resource_status compute_pass_close(doe_resource_compute_pass *pass)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (pass->debug_depth) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    if (pass->timestamps.set && pass->timestamps.end_index != UINT32_MAX) {
        status = resource_record_timestamp(pass->encoder, pass->timestamps.set,
            pass->timestamps.end_index, DOE_TIMESTAMP_PASS_END);
        if (status != DOE_RESOURCE_OK) return status;
    }
    doe_resource_query_set_release(pass->timestamps.set);
    pass->timestamps.set = NULL;
    pass->ended = true;
    pass->encoder->active_pass = NULL;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_compute_pass_debug(doe_resource_compute_pass *pass,
    doe_resource_debug_operation operation)
{
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    return resource_encoder_fail(pass->encoder, resource_debug_apply(&pass->debug_depth, operation));
}

doe_resource_status doe_resource_compute_pass_invalidate(doe_resource_compute_pass *pass,
    doe_resource_status rejection)
{
    if (!resource_status_is_rejection(rejection)) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    return resource_encoder_fail(pass->encoder, rejection);
}
