/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>

doe_dx_status doe_dx_commands_draw_mesh(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, const doe_dx_mesh_draw_desc *mesh, doe_dx_error *error) {
    doe_dx_status status = doe_dx_recording(commands, error);
    if (status != DOE_DX_OK) return status;
    const doe_dx_draw_desc *draw = mesh ? &mesh->draw : NULL;
    if (!pipeline || !draw || !draw->color || (!mesh->indices && !draw->vertex_count) || !draw->instance_count)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.draw", error);
    if (!pipeline->graphics)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.graphics_pipeline", error);
    if ((!mesh->indices && draw->vertex_count - 1 > UINT32_MAX - draw->first_vertex) ||
        draw->instance_count - 1 > UINT32_MAX - draw->first_instance)
        return doe_dx_fail(DOE_DX_RANGE, 0, "commands.draw.counts", error);
    if (draw->clear) {
        for (size_t index = 0; index < 4; index++)
            if (!isfinite(draw->clear_color[index]) || draw->clear_color[index] < 0 || draw->clear_color[index] > 1)
                return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.draw.clear", error);
    }
    status = doe_dx_texture_preflight(commands, draw->color, DOE_DX_TEXTURE_RENDER_ATTACHMENT, !draw->clear);
    if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "commands.draw.attachment", error);
    if (draw->color->desc.format != pipeline->color_format)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.draw.format", error);
    doe_dx_native_resources native = {0};
    doe_dx_reservation needed = {0};
    status = doe_dx_prepare_resources(commands, pipeline, resources, &native, &needed, error);
    if (status != DOE_DX_OK) return status;
    for (size_t index = 0; index < resources->texture_count; index++)
        if (resources->textures[index] == draw->color)
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "commands.draw.attachment_alias", error);
    doe_dx_native_geometry geometry = {0};
    status = doe_dx_prepare_geometry(commands, pipeline, resources, mesh, &geometry, &needed, error);
    if (status != DOE_DX_OK) return status;
    if (!doe_dx_find_texture(commands, draw->color)) needed.textures++;
    status = doe_dx_reserve_uses(commands, needed, error);
    if (status != DOE_DX_OK) return status;
    status = doe_dx_native_draw(commands->native, pipeline->native, &native, draw->color->native, &geometry, mesh, error);
    if (status != DOE_DX_OK) return doe_dx_context_result(commands->context, status, error);
    doe_dx_use_resources(commands, pipeline, resources);
    doe_dx_use_texture(commands, draw->color);
    doe_dx_use_geometry(commands, mesh);
    if (draw->clear) doe_dx_find_texture(commands, draw->color)->initialized = true;
    return doe_dx_fail(DOE_DX_OK, 0, "commands.draw", error);
}

doe_dx_status doe_dx_commands_draw(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, const doe_dx_draw_desc *draw, doe_dx_error *error) {
    if (!draw) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "commands.draw", error);
    doe_dx_mesh_draw_desc mesh = {0};
    mesh.draw = *draw;
    return doe_dx_commands_draw_mesh(commands, pipeline, resources, &mesh, error);
}
