/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

enum { MAX_BUNDLE_OPERATIONS = 4096 };

struct doe_resource_bundle_encoder {
    resource_base base;
    doe_resource_render_bundle_desc desc;
    doe_resource_status status;
    bool finished;
    size_t operations, count, capacity, debug_depth;
    doe_resource_render_packet state;
    doe_resource_render_packet *draws;
    resource_usage_scope scope;
};
struct doe_resource_render_bundle {
    resource_base base;
    doe_resource_render_bundle_desc desc;
    size_t count, capacity;
    doe_resource_render_packet *draws;
    resource_usage_scope scope;
    bool invalid; /* Returned for a failed finish; never executable. */
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
    doe_resource_render_bundle_desc normalized = *desc;
    doe_resource_status status = resource_color_formats_normalize(desc->color_format, desc->color_count,
        desc->color_formats, &normalized.color_count, normalized.color_formats);
    normalized.color_format = WGPUTextureFormat_Undefined;
    if (status == DOE_RESOURCE_OK) status = resource_color_layout_validate(config, normalized.color_formats,
        normalized.color_count, desc->depth_stencil_format, desc->samples);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_bundle_encoder *encoder = resource_allocate(config, sizeof(*encoder), _Alignof(doe_resource_bundle_encoder));
    if (!encoder) return DOE_RESOURCE_OUT_OF_MEMORY;
    memset(encoder, 0, sizeof(*encoder));
    encoder->desc = normalized;
    resource_init(&encoder->base, config, (doe_resource_native){0});
    *out = encoder;
    return DOE_RESOURCE_OK;
}

doe_resource_bundle_encoder *doe_resource_bundle_encoder_rejected(const doe_resource_config *config,
    doe_resource_status rejection)
{
    if (!resource_config_valid(config) || !resource_status_is_rejection(rejection)) return NULL;
    doe_resource_bundle_encoder *encoder = resource_allocate(config, sizeof(*encoder), _Alignof(doe_resource_bundle_encoder));
    if (!encoder) return NULL;
    memset(encoder, 0, sizeof(*encoder));
    encoder->status = rejection;
    resource_init(&encoder->base, config, (doe_resource_native){0});
    return encoder;
}

void doe_resource_bundle_encoder_retain(doe_resource_bundle_encoder *encoder)
{
    if (encoder) resource_retain(&encoder->base);
}

void doe_resource_bundle_encoder_release(doe_resource_bundle_encoder *encoder)
{
    if (!encoder || !resource_last_reference(&encoder->base)) return;
    resource_render_packet_release(&encoder->state);
    resource_scope_release(&encoder->base.config, &encoder->scope);
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
    status = resource_render_set_pipeline(encoder->base.config.device.identity, &encoder->state, &encoder->desc, pipeline);
    return complete_operation(encoder, status);
}

doe_resource_status doe_resource_bundle_encoder_set_group(doe_resource_bundle_encoder *encoder,
    uint32_t index, const doe_resource_dispatch_group *group)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_group(&encoder->base.config, &encoder->scope, &encoder->state, index, group);
    return complete_operation(encoder, status);
}

doe_resource_status doe_resource_bundle_encoder_set_vertex(doe_resource_bundle_encoder *encoder,
    uint32_t slot, doe_resource_buffer *buffer, uint64_t offset, uint64_t size)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_vertex(&encoder->base.config, &encoder->scope, &encoder->state, slot, buffer, offset, size);
    return complete_operation(encoder, status);
}

doe_resource_status doe_resource_bundle_encoder_set_index(doe_resource_bundle_encoder *encoder,
    doe_resource_buffer *buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size)
{
    doe_resource_status status = admit_operation(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_index(&encoder->base.config, &encoder->scope, &encoder->state, buffer, format, offset, size);
    return complete_operation(encoder, status);
}

static doe_resource_status append_draw(doe_resource_bundle_encoder *encoder,
    const doe_resource_render_packet *packet)
{
    doe_resource_status status = resource_render_draw_validate(packet, encoder->base.config.device.identity, &encoder->desc);
    if (status != DOE_RESOURCE_OK) return fail(encoder, status);
    if (packet->indirect) {
        status = resource_scope_input(&encoder->base.config, &encoder->scope, packet->indirect);
        if (status != DOE_RESOURCE_OK) return fail(encoder, status);
    }
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
    if (encoder) encoder->finished = true;
    if (status != DOE_RESOURCE_OK) return status;
    if (encoder->debug_depth) return fail(encoder, DOE_RESOURCE_STATE);
    doe_resource_render_bundle *bundle = resource_allocate(&encoder->base.config,
        sizeof(*bundle), _Alignof(doe_resource_render_bundle));
    if (!bundle) return fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    bundle->desc = encoder->desc;
    bundle->invalid = false;
    bundle->draws = encoder->draws;
    bundle->count = encoder->count;
    bundle->capacity = encoder->capacity;
    bundle->scope = encoder->scope;
    encoder->scope = (resource_usage_scope){0};
    resource_init(&bundle->base, &encoder->base.config, (doe_resource_native){0});
    encoder->draws = NULL;
    encoder->count = encoder->capacity = 0;
    encoder->finished = true;
    resource_render_packet_release(&encoder->state);
    memset(&encoder->state, 0, sizeof(encoder->state));
    *out = bundle;
    return DOE_RESOURCE_OK;
}

bool doe_resource_bundle_encoder_ended(const doe_resource_bundle_encoder *encoder)
{
    return encoder && encoder->finished;
}

doe_resource_render_bundle *doe_resource_render_bundle_rejected(doe_resource_bundle_encoder *encoder)
{
    if (!encoder || encoder->status == DOE_RESOURCE_OK) return NULL;
    doe_resource_render_bundle *bundle = resource_allocate(&encoder->base.config,
        sizeof(*bundle), _Alignof(doe_resource_render_bundle));
    if (!bundle) return NULL;
    memset(bundle, 0, sizeof(*bundle));
    bundle->desc = encoder->desc;
    bundle->invalid = true;
    resource_init(&bundle->base, &encoder->base.config, (doe_resource_native){0});
    return bundle;
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
    resource_scope_release(&bundle->base.config, &bundle->scope);
    resource_finalize(&bundle->base, sizeof(*bundle), _Alignof(doe_resource_render_bundle));
}

doe_resource_status doe_resource_render_pass_execute_bundles(doe_resource_render_pass *pass,
    doe_resource_render_bundle *const *bundles, size_t count)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (count && !bundles) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    doe_resource_render_bundle_desc signature = resource_render_targets_layout(&pass->state.targets);
    uint64_t remaining = pass->state.targets.max_draw_count - pass->draw_count;
    for (size_t i = 0; i < count; i++) {
        doe_resource_render_bundle *bundle = bundles[i];
        if (!bundle || bundle->invalid || !resource_alive(&bundle->base))
            return resource_encoder_fail(pass->encoder, DOE_RESOURCE_UNAVAILABLE);
        if (bundle->base.config.device.identity != pass->base.config.device.identity)
            return resource_encoder_fail(pass->encoder, DOE_RESOURCE_DEVICE_MISMATCH);
        if (!resource_color_layouts_equal(bundle->desc.color_count, bundle->desc.color_formats,
                signature.color_count, signature.color_formats) ||
            bundle->desc.depth_stencil_format != signature.depth_stencil_format ||
            bundle->desc.samples != signature.samples ||
            (signature.depth_read_only && !bundle->desc.depth_read_only) ||
            (signature.stencil_read_only && !bundle->desc.stencil_read_only))
            return resource_encoder_fail(pass->encoder, DOE_RESOURCE_LAYOUT);
        status = resource_scope_validate(&bundle->scope);
        if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
        if (bundle->count > remaining) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
        remaining -= bundle->count;
        for (size_t j = 0; j < bundle->count; j++) {
            status = resource_render_draw_validate(&bundle->draws[j], pass->base.config.device.identity, &signature);
            if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
        }
    }
    for (size_t i = 0; i < count; i++) {
        status = resource_scope_add(&pass->base.config, &pass->scope, bundles[i]->scope.uses, bundles[i]->scope.count);
        if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
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

void *doe_resource_bundle_encoder_device(const doe_resource_bundle_encoder *encoder)
{
    return encoder ? encoder->base.config.device.identity : NULL;
}
const doe_resource_render_bundle_desc *doe_resource_bundle_encoder_description(const doe_resource_bundle_encoder *encoder)
{
    return encoder ? &encoder->desc : NULL;
}
void *doe_resource_render_bundle_device(const doe_resource_render_bundle *bundle)
{
    return bundle ? bundle->base.config.device.identity : NULL;
}
const doe_resource_render_bundle_desc *doe_resource_render_bundle_description(const doe_resource_render_bundle *bundle)
{
    return bundle ? &bundle->desc : NULL;
}

doe_resource_status doe_resource_bundle_encoder_invalidate(doe_resource_bundle_encoder *encoder,
    doe_resource_status rejection)
{
    if (!resource_status_is_rejection(rejection)) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = open_encoder(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    return fail(encoder, rejection);
}

doe_resource_status doe_resource_bundle_encoder_debug(doe_resource_bundle_encoder *encoder,
    doe_resource_debug_operation operation)
{
    doe_resource_status status = open_encoder(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    return fail(encoder, resource_debug_apply(&encoder->debug_depth, operation));
}
