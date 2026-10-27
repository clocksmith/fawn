/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

static doe_mtl_status retain_pipeline(doe_mtl_commands *commands, doe_mtl_render_pipeline *pipeline) {
    doe_mtl_render_pipeline_use *use;
    doe_mtl_status status;
    for (use = commands->render_pipelines; use != NULL; use = use->next)
        if (use->pipeline == pipeline) return DOE_MTL_OK;
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    status = doe_mtl_render_pipeline_retain(pipeline);
    if (status != DOE_MTL_OK) {
        free(use);
        return status;
    }
    use->pipeline = pipeline;
    use->next = commands->render_pipelines;
    commands->render_pipelines = use;
    return DOE_MTL_OK;
}
static doe_mtl_status retain_bindings(doe_mtl_commands *commands, const doe_mtl_render_bindings *bindings) {
    size_t i;
    doe_mtl_buffer_use *use;
    doe_mtl_status status = DOE_MTL_OK;
    for (i = 0; i < bindings->buffer_count && status == DOE_MTL_OK; ++i)
        status = doe_mtl_use_buffer(commands, bindings->buffers[i].buffer, &use);
    if (status == DOE_MTL_OK) status = doe_mtl_image_bindings_retain(commands,
        bindings->textures, bindings->texture_count, bindings->samplers, bindings->sampler_count);
    return status;
}
doe_mtl_status doe_mtl_render_draw_retain(doe_mtl_commands *commands, const doe_mtl_render_draw *draw) {
    doe_mtl_buffer_use *use;
    doe_mtl_status status = retain_pipeline(commands, draw->pipeline);
    if (status == DOE_MTL_OK) status = retain_bindings(commands, &draw->vertex);
    if (status == DOE_MTL_OK) status = retain_bindings(commands, &draw->fragment);
    if (status == DOE_MTL_OK && draw->index_buffer != NULL) status = doe_mtl_use_buffer(commands, draw->index_buffer, &use);
    return status;
}
void doe_mtl_render_retire(doe_mtl_commands *commands) {
    while (commands->render_pipelines != NULL) {
        doe_mtl_render_pipeline_use *use = commands->render_pipelines;
        commands->render_pipelines = use->next;
        doe_mtl_render_pipeline_release(use->pipeline);
        free(use);
    }
}
doe_mtl_status doe_mtl_commands_render(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, const doe_mtl_render_draw *draws,
    size_t draw_count, doe_mtl_error *error) {
    doe_mtl_status status = doe_mtl_recording(commands, error);
    doe_mtl_native_render_pass native_pass = {0};
    doe_mtl_native_render_draw *native_draws = NULL;
    doe_mtl_native *native;
    doe_mtl_error detail = {0};
    size_t i;
    if (status != DOE_MTL_OK) return status;
    if ((draw_count != 0 && draws == NULL) || draw_count > SIZE_MAX / sizeof(*native_draws))
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "render draws", error);
    native = &commands->context->native;
    if (native->render == NULL) return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "render native boundary", error);
    status = doe_mtl_render_pass_preflight(commands, pass, &native_pass);
    if (status != DOE_MTL_OK) return doe_mtl_poison(commands, status, "render attachments", error);
    if (draw_count != 0) {
        native_draws = calloc(draw_count, sizeof(*native_draws));
        if (native_draws == NULL) return doe_mtl_poison(commands, DOE_MTL_OUT_OF_MEMORY, "render draw snapshot", error);
    }
    /* Validate the complete pass before the first native encoder mutation. */
    for (i = 0; i < draw_count && status == DOE_MTL_OK; ++i)
        status = doe_mtl_render_draw_preflight(commands, pass, &native_pass, &draws[i], &native_draws[i]);
    if (status == DOE_MTL_OK) status = doe_mtl_render_visibility_preflight(commands, pass, draws, draw_count);
    if (status == DOE_MTL_OK) status = doe_mtl_render_pass_retain(commands, pass);
    for (i = 0; i < draw_count && status == DOE_MTL_OK; ++i)
        status = doe_mtl_render_draw_retain(commands, &draws[i]);
    if (status != DOE_MTL_OK) {
        free(native_draws);
        return doe_mtl_poison(commands, status, "render validation/lease", error);
    }
    for (i = 0; i < draw_count && status == DOE_MTL_OK; ++i) {
        if (i && native_draws[i-1].indirect_arguments &&
            doe_mtl_draw_guard_same_inputs(&draws[i-1], &draws[i])) {
            /* The existing command lease owns this immutable scratch. Reuse
             * only here; every original draw still reaches the native pass. */
            native_draws[i].indirect_arguments = native_draws[i-1].indirect_arguments;
        } else status = doe_mtl_draw_guard_prepare(commands, &draws[i], &native_draws[i], &detail);
    }
    if (status != DOE_MTL_OK) {
        free(native_draws);
        return doe_mtl_native_record_result(commands, status, &detail, error);
    }
    status = native->render(native->userdata, commands->native, &native_pass, native_draws, draw_count, &detail);
    free(native_draws);
    if (status == DOE_MTL_OK) doe_mtl_render_pass_publish(commands, pass);
    return doe_mtl_native_record_result(commands, status, &detail, error);
}
