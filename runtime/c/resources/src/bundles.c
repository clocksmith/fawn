/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

enum { MAX_BUNDLE_OPERATIONS = 4096 };

struct doe_resource_bundle_encoder {
    resource_base base;
    doe_resource_render_bundle_desc desc;
    doe_resource_status status;
    bool finished;
    size_t operations, count, capacity;
    doe_resource_render_packet state;
    doe_resource_render_packet *draws;
};
struct doe_resource_render_bundle {
    resource_base base;
    doe_resource_render_bundle_desc desc;
    size_t count, capacity;
    doe_resource_render_packet *draws;
};

static doe_resource_status fail(doe_resource_bundle_encoder *encoder, doe_resource_status status)
{
    if (!encoder) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (encoder->status == DOE_RESOURCE_OK) encoder->status = status;
    return encoder->status;
}

static doe_resource_status open_encoder(doe_resource_bundle_encoder *encoder)
{
    if (!encoder) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (encoder->status != DOE_RESOURCE_OK) return encoder->status;
    if (encoder->finished) return fail(encoder, DOE_RESOURCE_STATE);
    if (!resource_alive(&encoder->base)) return fail(encoder, DOE_RESOURCE_UNAVAILABLE);
    return DOE_RESOURCE_OK;
}

static doe_resource_status admit_operation(doe_resource_bundle_encoder *encoder)
{
    doe_resource_status status = open_encoder(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    if (encoder->operations == MAX_BUNDLE_OPERATIONS) return fail(encoder, DOE_RESOURCE_RANGE);
    return DOE_RESOURCE_OK;
}

static doe_resource_status complete_operation(doe_resource_bundle_encoder *encoder, doe_resource_status status)
{
    if (status != DOE_RESOURCE_OK) return fail(encoder, status);
    encoder->operations++;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_bundle_encoder_create(const doe_resource_config *config,
    const doe_resource_render_bundle_desc *desc, doe_resource_bundle_encoder **out)
{
    if (!resource_config_valid(config) || !desc || !out) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    uint32_t bytes, width, height;
    doe_resource_status status = resource_format_block(desc->color_format, &bytes, &width, &height);
    if (status != DOE_RESOURCE_OK) return status;
    if (width != 1 || height != 1 || resource_format_depth(desc->color_format) ||
        (desc->depth_stencil_format && !resource_format_depth(desc->depth_stencil_format))) return DOE_RESOURCE_LAYOUT;
    if (desc->samples != 1 && desc->samples != 4) return DOE_RESOURCE_UNSUPPORTED;
    doe_resource_bundle_encoder *encoder = resource_allocate(config, sizeof(*encoder), _Alignof(doe_resource_bundle_encoder));
    if (!encoder) return DOE_RESOURCE_OUT_OF_MEMORY;
    memset(encoder, 0, sizeof(*encoder));
    encoder->desc = *desc;
    resource_init(&encoder->base, config, (doe_resource_native){0});
    *out = encoder;
    return DOE_RESOURCE_OK;
}

void doe_resource_bundle_encoder_retain(doe_resource_bundle_encoder *encoder)
{
    if (encoder) resource_retain(&encoder->base);
}

void doe_resource_bundle_encoder_release(doe_resource_bundle_encoder *encoder)
{
    if (!encoder || !resource_last_reference(&encoder->base)) return;
    resource_render_packet_release(&encoder->state);
    for (size_t i = 0; i < encoder->count; i++) resource_render_packet_release(&encoder->draws[i]);
    resource_free(&encoder->base.config, encoder->draws,
        encoder->capacity * sizeof(*encoder->draws), _Alignof(doe_resource_render_packet));
    resource_finalize(&encoder->base, sizeof(*encoder), _Alignof(doe_resource_bundle_encoder));
}

doe_resource_status doe_resource_bundle_encoder_set_pipeline(doe_resource_bundle_encoder *encoder,
    doe_resource_render_pipeline *pipeline)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_pipeline(encoder->base.config.device.identity, &encoder->state, pipeline);
    return complete_operation(encoder, status);
}

doe_resource_status doe_resource_bundle_encoder_set_group(doe_resource_bundle_encoder *encoder,
    uint32_t index, const doe_resource_dispatch_group *group)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_group(encoder->base.config.device.identity, &encoder->state, index, group);
    return complete_operation(encoder, status);
}

doe_resource_status doe_resource_bundle_encoder_set_vertex(doe_resource_bundle_encoder *encoder,
    uint32_t slot, doe_resource_buffer *buffer, uint64_t offset, uint64_t size)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_vertex(encoder->base.config.device.identity, &encoder->state, slot, buffer, offset, size);
    return complete_operation(encoder, status);
}

doe_resource_status doe_resource_bundle_encoder_set_index(doe_resource_bundle_encoder *encoder,
    doe_resource_buffer *buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_index(encoder->base.config.device.identity, &encoder->state, buffer, format, offset, size);
    return complete_operation(encoder, status);
}

static doe_resource_status append_draw(doe_resource_bundle_encoder *encoder,
    const doe_resource_render_packet *packet)
{
    doe_resource_status status = resource_render_draw_validate(packet, encoder->base.config.device.identity, &encoder->desc);
    if (status != DOE_RESOURCE_OK) return fail(encoder, status);
    if (encoder->count == encoder->capacity) {
        size_t capacity = encoder->capacity ? encoder->capacity * 2 : 8;
        if (capacity > MAX_BUNDLE_OPERATIONS) capacity = MAX_BUNDLE_OPERATIONS;
        if (capacity > SIZE_MAX / sizeof(*encoder->draws)) return fail(encoder, DOE_RESOURCE_OVERFLOW);
        doe_resource_render_packet *draws = resource_allocate(&encoder->base.config,
            capacity * sizeof(*draws), _Alignof(doe_resource_render_packet));
        if (!draws) return fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
        if (encoder->count) memcpy(draws, encoder->draws, encoder->count * sizeof(*draws));
        resource_free(&encoder->base.config, encoder->draws,
            encoder->capacity * sizeof(*draws), _Alignof(doe_resource_render_packet));
        encoder->draws = draws;
        encoder->capacity = capacity;
    }
    resource_render_packet_retain(packet);
    encoder->draws[encoder->count++] = *packet;
    return complete_operation(encoder, DOE_RESOURCE_OK);
}

doe_resource_status doe_resource_bundle_encoder_draw(doe_resource_bundle_encoder *encoder,
    bool indexed, uint32_t count, uint32_t instances, uint32_t first,
    int32_t base_vertex, uint32_t first_instance)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_render_packet packet = encoder->state;
    packet.indexed = indexed;
    packet.count = count;
    packet.instances = instances;
    packet.first = first;
    packet.base_vertex = base_vertex;
    packet.first_instance = first_instance;
    return append_draw(encoder, &packet);
}

doe_resource_status doe_resource_bundle_encoder_draw_indirect(doe_resource_bundle_encoder *encoder,
    doe_resource_buffer *arguments, uint64_t offset, bool indexed)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    if (!arguments) return fail(encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    doe_resource_render_packet packet = encoder->state;
    packet.indexed = indexed;
    packet.indirect = arguments;
    packet.indirect_offset = offset;
    return append_draw(encoder, &packet);
}

doe_resource_status doe_resource_bundle_encoder_finish(doe_resource_bundle_encoder *encoder,
    doe_resource_render_bundle **out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = open_encoder(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_render_bundle *bundle = resource_allocate(&encoder->base.config,
        sizeof(*bundle), _Alignof(doe_resource_render_bundle));
    if (!bundle) return fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    bundle->desc = encoder->desc;
    bundle->draws = encoder->draws;
    bundle->count = encoder->count;
    bundle->capacity = encoder->capacity;
    resource_init(&bundle->base, &encoder->base.config, (doe_resource_native){0});
    encoder->draws = NULL;
    encoder->count = encoder->capacity = 0;
    encoder->finished = true;
    resource_render_packet_release(&encoder->state);
    memset(&encoder->state, 0, sizeof(encoder->state));
    *out = bundle;
    return DOE_RESOURCE_OK;
}

void doe_resource_render_bundle_retain(doe_resource_render_bundle *bundle)
{
    if (bundle) resource_retain(&bundle->base);
}

void doe_resource_render_bundle_release(doe_resource_render_bundle *bundle)
{
    if (!bundle || !resource_last_reference(&bundle->base)) return;
    for (size_t i = 0; i < bundle->count; i++) resource_render_packet_release(&bundle->draws[i]);
    resource_free(&bundle->base.config, bundle->draws,
        bundle->capacity * sizeof(*bundle->draws), _Alignof(doe_resource_render_packet));
    resource_finalize(&bundle->base, sizeof(*bundle), _Alignof(doe_resource_render_bundle));
}

doe_resource_status doe_resource_render_pass_execute_bundles(doe_resource_render_pass *pass,
    doe_resource_render_bundle *const *bundles, size_t count)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (count && !bundles) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    doe_resource_render_bundle_desc signature = {
        pass->state.targets.color->desc.format,
        pass->state.targets.depth_stencil ? pass->state.targets.depth_stencil->desc.format : 0,
        pass->state.targets.color->texture->desc.samples,
        pass->state.targets.depth_read_only, pass->state.targets.stencil_read_only
    };
    uint64_t remaining = pass->state.targets.max_draw_count - pass->draw_count;
    for (size_t i = 0; i < count; i++) {
        doe_resource_render_bundle *bundle = bundles[i];
        if (!bundle || !resource_alive(&bundle->base)) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_UNAVAILABLE);
        if (bundle->base.config.device.identity != pass->base.config.device.identity)
            return resource_encoder_fail(pass->encoder, DOE_RESOURCE_DEVICE_MISMATCH);
        if (bundle->desc.color_format != signature.color_format ||
            bundle->desc.depth_stencil_format != signature.depth_stencil_format ||
            bundle->desc.samples != signature.samples) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_LAYOUT);
        if (bundle->count > remaining) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
        remaining -= bundle->count;
        for (size_t j = 0; j < bundle->count; j++) {
            status = resource_render_draw_validate(&bundle->draws[j], pass->base.config.device.identity, &signature);
            if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
        }
    }
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < bundles[i]->count; j++) {
            status = resource_render_replay_packet(pass, &bundles[i]->draws[j]);
            if (status != DOE_RESOURCE_OK) return status;
        }
    }
    /* Logical WebGPU state clears even for an empty list. Backend physical
     * state restoration does not preserve these public binding obligations. */
    resource_render_clear_bindings(&pass->state);
    return DOE_RESOURCE_OK;
}
