/* SPDX-License-Identifier: MIT */
#include "metal_graphics_internal.h"
#include "metal_render_bindings.h"
#include <stdlib.h>
#include <string.h>

/* Binding-cache state of each change of binding packet, in pass order. */
typedef struct run_states {
    size_t *values;
    size_t count, capacity;
} run_states;

static bool push_state(run_states *states, size_t value)
{
    if (states->count == states->capacity) {
        size_t capacity = states->capacity ? states->capacity * 2 : 64;
        if (capacity < states->capacity || capacity > SIZE_MAX / sizeof(*states->values)) return false;
        size_t *values = realloc(states->values, capacity * sizeof(*values));
        if (!values) return false;
        states->values = values;
        states->capacity = capacity;
    }
    states->values[states->count++] = value;
    return true;
}

/* Draws sharing one envelope and binding packet (a draw run, see
 * doe_resource_render_record_draw_borrow) share every native draw field except
 * their own arguments: the template is built once per run and copied per draw. */
typedef struct draw_template {
    const doe_resource_render_packet *bindings, *envelope;
    native_device *device;
    doe_mtl_render_draw draw; /* Arguments, index and guard fields unset. */
} draw_template;

static native_result template_for(const doe_resource_render_packet *bindings,
    const doe_resource_render_packet *envelope, const native_metal_render_binding_view *view, draw_template *out)
{
    native_pipeline *pipeline = doe_resource_render_pipeline_native(bindings->pipeline);
    if (!pipeline || !pipeline->metal_graphics) return NATIVE_INVALID_ARGUMENT;
    const native_metal_graphics *graphics = pipeline->metal_graphics;
    if (!view) return NATIVE_INVALID_STATE;
    const doe_resource_render_state *state = &envelope->state;
    doe_vk_clear_color blend;
    if (!native_render_clear(WGPUTextureFormat_RGBA32Float, state->blend, &blend)) return NATIVE_INVALID_ARGUMENT;
    out->bindings = bindings;
    out->envelope = envelope;
    out->device = pipeline->device;
    out->draw = (doe_mtl_render_draw){.pipeline=pipeline->handle.metal_render,
        .topology=graphics->topology, .winding=graphics->winding, .cull=graphics->cull,
        .viewport_enabled=true, .scissor_enabled=true,
        .viewport={state->viewport[0],state->viewport[1],state->viewport[2],
            state->viewport[3],state->viewport[4],state->viewport[5]},
        .scissor={state->scissor[0],state->scissor[1],state->scissor[2],state->scissor[3]},
        .depth_bias=graphics->depth_bias, .depth_slope=graphics->depth_slope,
        .depth_clamp=graphics->depth_clamp, .unclipped_depth=graphics->unclipped_depth,
        .stencil_reference=state->stencil_reference,
        .visibility_mode=envelope->occlusion_active ? 1u : 0u,
        .visibility_offset=envelope->occlusion_active ? (uint64_t)envelope->occlusion_index * 8 : 0};
    memcpy(out->draw.blend_constant, blend.float32, sizeof(out->draw.blend_constant));
    out->draw.vertex = view->vertex;
    out->draw.fragment = view->fragment;
    /* The binding packet lives in command-owned storage until the commands retire. */
    out->draw.vertex.immediates = (doe_mtl_immediate_binding){bindings->immediates.words,
        graphics->stages[0].immediate_bytes};
    out->draw.fragment.immediates = (doe_mtl_immediate_binding){bindings->immediates.words,
        graphics->stages[1].immediate_bytes};
    return NATIVE_OK;
}

static native_result prepare_draw(const draw_template *run, const doe_resource_render_draw_args *args,
    doe_mtl_guarded_draw *guarded, doe_mtl_render_draw *draw)
{
    *draw = run->draw;
    draw->count = args->count;
    draw->instance_count = args->instances;
    draw->first_vertex = args->indexed ? 0 : args->first;
    draw->first_instance = args->first_instance;
    if (!args->indexed && !args->indirect) return NATIVE_OK;
    native_device *device = run->device;
    if (!guarded || !device->metal_draw_guard) return NATIVE_INVALID_STATE;
    native_buffer *arguments = doe_resource_buffer_native(args->indirect);
    *guarded = (doe_mtl_guarded_draw){.guard=device->metal_draw_guard,
        .arguments=arguments ? arguments->handle.metal : NULL,.argument_offset=args->indirect_offset,
        .first_index=args->first,.first_instance_allowed=!args->indirect ||
            native_feature_enabled(device,WGPUFeatureName_IndirectFirstInstance)};
    if (args->indexed) {
        const doe_resource_render_packet *packet = run->bindings;
        native_buffer *index = doe_resource_buffer_native(packet->index.buffer);
        if (!index) return NATIVE_INVALID_ARGUMENT;
        switch (packet->index_format) {
        case WGPUIndexFormat_Uint16: draw->index_type = 0; break;
        case WGPUIndexFormat_Uint32: draw->index_type = 1; break;
        case WGPUIndexFormat_Undefined:
        case WGPUIndexFormat_Force32: return NATIVE_INVALID_ARGUMENT;
        }
        draw->index_buffer = index->handle.metal;
        draw->index_offset = packet->index.offset;
        draw->base_vertex = args->base_vertex;
        guarded->index_size = packet->index.size;
    }
    draw->guarded = guarded;
    return NATIVE_OK;
}

static bool clear_color(const doe_resource_color_attachment *source, doe_mtl_clear_color *out)
{
    const doe_resource_view_desc *view = doe_resource_view_description(source->view);
    doe_vk_clear_color converted;
    uint32_t scalar;
    if (!view || !native_color_scalar(view->format, &scalar) ||
        !native_render_clear(view->format, source->clear, &converted)) return false;
    double values[4];
    for (size_t i = 0; i < 4; ++i) values[i] = scalar == DOE_COMPILER_LEAF_F32 ? converted.float32[i] :
        scalar == DOE_COMPILER_LEAF_U32 ? (double)converted.uint32[i] : (double)converted.int32[i];
    *out = (doe_mtl_clear_color){values[0],values[1],values[2],values[3]};
    return true;
}

static bool same_targets(const doe_resource_render_targets *a, const doe_resource_render_targets *b)
{
    if (a->color_count != b->color_count || a->depth_stencil != b->depth_stencil ||
        a->occlusion_queries != b->occlusion_queries) return false;
    for (uint32_t i = 0; i < a->color_count; ++i)
        if (a->colors[i].view != b->colors[i].view || a->colors[i].resolve_target != b->colors[i].resolve_target) return false;
    return true;
}

native_failure native_metal_render_timed(doe_mtl_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed,
    const doe_mtl_pass_timestamps *timestamps)
{
    if (!count || records[0].kind != DOE_COMMAND_RENDER) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_resource_render_packet *begin = doe_resource_render_record_borrow(records[0].value.render);
    if (!begin || !begin->boundary_only || !begin->first_in_pass || begin->last_in_pass)
        return native_failure_status(NATIVE_UNSUPPORTED);
    size_t end = 0, draws_count = 0, guarded_count = 0;
    native_metal_render_binding_cache *bindings = NULL;
    const doe_resource_render_packet *interned = NULL;
    run_states states = {0};
    native_failure failure = native_failure_status(NATIVE_OK);
    for (size_t i = 1; failure.status == NATIVE_OK && i < count; ++i) {
        if (records[i].kind == DOE_COMMAND_BEGIN_OCCLUSION || records[i].kind == DOE_COMMAND_END_OCCLUSION) {
            if (records[i].value.timestamp.set != begin->targets.occlusion_queries)
                failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
            continue;
        }
        if (records[i].kind != DOE_COMMAND_RENDER) { failure = native_failure_status(NATIVE_UNSUPPORTED); break; }
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
        if (!packet || packet->first_in_pass || !same_targets(&packet->targets, &begin->targets)) {
            failure = native_failure_status(NATIVE_INVALID_ARGUMENT); break;
        }
        if (packet->boundary_only) {
            if (!packet->last_in_pass) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
            end = i; break;
        }
        if (packet->last_in_pass) { failure = native_failure_status(NATIVE_INVALID_ARGUMENT); break; }
        size_t draws = doe_resource_render_record_draw_count(records[i].value.render);
        if (draws > SIZE_MAX - draws_count) { failure = native_failure_status(NATIVE_OUT_OF_MEMORY); break; }
        draws_count += draws;
        for (size_t j = 0; failure.status == NATIVE_OK && j < draws; ++j) {
            const doe_resource_render_packet *draw = NULL;
            doe_resource_render_draw_args args;
            if (!doe_resource_render_record_draw_borrow(records[i].value.render, j, &draw, &args)) {
                failure = native_failure_status(NATIVE_INVALID_ARGUMENT); break;
            }
            if (draw != interned) {
                size_t state = 0;
                failure = native_failure_status(native_metal_render_bindings_intern_index(&bindings, draw, &state));
                if (failure.status == NATIVE_OK && !push_state(&states, state))
                    failure = native_failure_status(NATIVE_OUT_OF_MEMORY);
                interned = draw;
            }
            if (args.indexed || args.indirect) ++guarded_count;
        }
    }
    if (failure.status == NATIVE_OK && !end) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (draws_count > SIZE_MAX / sizeof(doe_mtl_render_draw))
        failure = native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_mtl_guarded_draw *guarded = NULL;
    doe_mtl_render_draw *draws = NULL;
    if (failure.status == NATIVE_OK && draws_count) {
        /* Every row is assigned before use; nothing reads an unassigned row. */
        draws = malloc(draws_count * sizeof(*draws));
        guarded = guarded_count ? malloc(guarded_count * sizeof(*guarded)) : NULL;
        if (!draws || (guarded_count && !guarded)) failure = native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    if (failure.status == NATIVE_OK) failure = native_metal_render_bindings_prepare(bindings, commands);
    size_t draw_index = 0, guarded_index = 0;
    draw_template run = {0};
    const doe_resource_render_packet *viewed = NULL;
    const native_metal_render_binding_view *view = NULL;
    size_t state_index = 0;
    native_result prepared = NATIVE_OK;
    for (size_t i = 1; failure.status == NATIVE_OK && prepared == NATIVE_OK && i < end; ++i) {
        if (records[i].kind != DOE_COMMAND_RENDER) continue;
        const doe_resource_render_packet *envelope = doe_resource_render_record_borrow(records[i].value.render);
        for (size_t j = 0; prepared == NATIVE_OK &&
                j < doe_resource_render_record_draw_count(records[i].value.render); ++j) {
            const doe_resource_render_packet *packet = NULL;
            doe_resource_render_draw_args args;
            if (!doe_resource_render_record_draw_borrow(records[i].value.render, j, &packet, &args)) {
                prepared = NATIVE_INVALID_ARGUMENT; break;
            }
            /* Binding packets change here exactly where the first loop interned. */
            if (packet != viewed) {
                if (state_index >= states.count) { prepared = NATIVE_INVALID_STATE; break; }
                view = native_metal_render_bindings_view(bindings, states.values[state_index++]);
                viewed = packet;
            }
            if (packet != run.bindings || envelope != run.envelope)
                prepared = template_for(packet, envelope, view, &run);
            if (prepared != NATIVE_OK) break;
            bool guard = args.indexed || args.indirect;
            if (guard && guarded_index >= guarded_count) { prepared = NATIVE_INVALID_STATE; break; }
            prepared = prepare_draw(&run, &args, guard ? &guarded[guarded_index++] : NULL, &draws[draw_index]);
            ++draw_index;
        }
    }
    if (failure.status == NATIVE_OK && prepared != NATIVE_OK) failure = native_failure_status(prepared);
    doe_mtl_render_pass_desc pass = {.color_count=begin->targets.color_count};
    if (timestamps) pass.timestamps = *timestamps;
    doe_mtl_error detail = {0};
    if (failure.status == NATIVE_OK && begin->targets.occlusion_queries) {
        native_queries *queries = doe_resource_query_set_native(begin->targets.occlusion_queries);
        if (!queries) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        else pass.visibility_buffer = queries->metal_results;
        /* The resource owner admits each index once per pass. Reset only those
         * begun here: unused indices preserve earlier pass results, while an
         * empty begin/end writes zero without manufacturing a visibility hit. */
        for (size_t i = 1; failure.status == NATIVE_OK && i < end; ++i) {
            if (records[i].kind != DOE_COMMAND_BEGIN_OCCLUSION) continue;
            doe_mtl_status status = doe_mtl_commands_fill(commands, pass.visibility_buffer,
                (uint64_t)records[i].value.timestamp.index * 8, 8, 0, &detail);
            failure = native_failure_metal(status, &detail);
        }
    }
    for (uint32_t i = 0; failure.status == NATIVE_OK && i < pass.color_count; ++i) {
        const doe_resource_color_attachment *source = &begin->targets.colors[i];
        native_view *view = doe_resource_view_native(source->view);
        if (!view) continue;
        native_view *resolve = doe_resource_view_native(source->resolve_target);
        doe_mtl_render_attachment *target = &pass.colors[i];
        *target = (doe_mtl_render_attachment){.view=view->metal,
            .load=source->load == WGPULoadOp_Clear ? DOE_MTL_CLEAR : DOE_MTL_LOAD,
            .store=resolve ? (source->store == WGPUStoreOp_Store ? DOE_MTL_STORE_RESOLVE : DOE_MTL_RESOLVE) :
                source->store == WGPUStoreOp_Store ? DOE_MTL_STORE : DOE_MTL_STORE_DISCARD,
            .resolve_view=resolve ? resolve->metal : NULL};
        if (source->load == WGPULoadOp_Clear && !clear_color(source, &target->clear_color))
            failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        /* A Clear load overwrites the whole single-subresource attachment, so
         * only a Load needs the attachment's missing contents zeroed first. */
        if (failure.status == NATIVE_OK && source->load != WGPULoadOp_Clear) failure = native_failure_metal(
            doe_mtl_commands_initialize_attachment(commands, view->metal, &detail), &detail);
    }
    native_view *depth = doe_resource_view_native(begin->targets.depth_stencil);
    if (failure.status == NATIVE_OK && depth) {
        const doe_resource_render_targets *source = &begin->targets;
        const doe_resource_view_desc *view = doe_resource_view_description(source->depth_stencil);
        WGPUTextureFormat aspect;
        if (doe_resource_format_aspect_format(view->format, WGPUTextureAspect_DepthOnly, &aspect) == DOE_RESOURCE_OK)
            pass.depth = (doe_mtl_render_attachment){.view=depth->metal,
                .load=source->depth_load == WGPULoadOp_Clear ? DOE_MTL_CLEAR : DOE_MTL_LOAD,
                .store=source->depth_read_only || source->depth_store == WGPUStoreOp_Store ? DOE_MTL_STORE : DOE_MTL_STORE_DISCARD,
                .clear_depth=source->clear_depth};
        if (doe_resource_format_aspect_format(view->format, WGPUTextureAspect_StencilOnly, &aspect) == DOE_RESOURCE_OK)
            pass.stencil = (doe_mtl_render_attachment){.view=depth->metal,
                .load=source->stencil_load == WGPULoadOp_Clear ? DOE_MTL_CLEAR : DOE_MTL_LOAD,
                .store=source->stencil_read_only || source->stencil_store == WGPUStoreOp_Store ? DOE_MTL_STORE : DOE_MTL_STORE_DISCARD,
                .clear_stencil=source->clear_stencil};
        if (!(!pass.depth.view || pass.depth.load == DOE_MTL_CLEAR) || !(!pass.stencil.view || pass.stencil.load == DOE_MTL_CLEAR))
            failure = native_failure_metal(doe_mtl_commands_initialize_attachment(commands, depth->metal, &detail), &detail);
    }
    if (failure.status == NATIVE_OK) {
        doe_mtl_status status = doe_mtl_commands_render(commands, &pass, draws, draws_count, &detail);
        failure = native_failure_metal(status, &detail);
    }
    native_metal_render_bindings_release(bindings);
    free(draws);
    free(guarded);
    free(states.values);
    if (failure.status == NATIVE_OK) *consumed = end + 1;
    return failure;
}

native_failure native_metal_render(doe_mtl_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed)
{
    return native_metal_render_timed(commands, records, count, consumed, NULL);
}
