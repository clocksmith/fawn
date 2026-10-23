/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

static bool bound_as_input(const doe_mtl_render_bindings *bindings, const doe_mtl_buffer *buffer) {
    size_t i;
    for (i = 0; i < bindings->buffer_count; ++i)
        if (bindings->buffers[i].buffer == buffer) return true;
    return false;
}
doe_mtl_status doe_mtl_render_visibility_preflight(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, const doe_mtl_render_draw *draws, size_t count) {
    size_t i, j;
    for (i = 0; i < count; ++i) {
        const doe_mtl_render_draw *draw = &draws[i];
        if (draw->visibility_mode > 2) return DOE_MTL_INVALID_ARGUMENT;
        if (pass->visibility_buffer != NULL && (draw->index_buffer == pass->visibility_buffer ||
            bound_as_input(&draw->vertex, pass->visibility_buffer) ||
            bound_as_input(&draw->fragment, pass->visibility_buffer))) return DOE_MTL_INVALID_ARGUMENT;
        if (draw->visibility_mode == 0) {
            if (draw->visibility_offset != 0) return DOE_MTL_INVALID_ARGUMENT;
            continue;
        }
        if (pass->visibility_buffer == NULL) return DOE_MTL_INVALID_ARGUMENT;
        if (draw->visibility_mode == 2 && !commands->context->caps.counting_visibility) return DOE_MTL_UNSUPPORTED;
        if (draw->visibility_offset % 8 != 0) return DOE_MTL_ALIGNMENT;
        if (draw->visibility_offset > commands->context->caps.max_visibility_offset ||
            !doe_mtl_range(pass->visibility_buffer->size, draw->visibility_offset, 8)) return DOE_MTL_RANGE;
        if (i != 0 && draws[i - 1].visibility_mode != 0 &&
            draws[i - 1].visibility_offset == draw->visibility_offset) {
            if (draws[i - 1].visibility_mode != draw->visibility_mode) return DOE_MTL_INVALID_ARGUMENT;
            continue;
        }
        /* Metal permits each result offset only once per pass. Consecutive
         * draws share one native query configuration; disjoint reuse fails. */
        for (j = 0; j < i; ++j)
            if (draws[j].visibility_mode != 0 && draws[j].visibility_offset == draw->visibility_offset)
                return DOE_MTL_INVALID_ARGUMENT;
    }
    return DOE_MTL_OK;
}
