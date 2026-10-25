/* SPDX-License-Identifier: MIT */
#include "metal_binding_internal.h"

native_failure native_metal_binding_snapshot(doe_mtl_commands *commands,
    const native_pipeline *pipeline, const native_metal_stage_map *map,
    const doe_resource_binding *resolved, native_metal_snapshot *out)
{
    out->bindings = (doe_mtl_render_bindings){.buffers=out->buffers,
        .textures=out->textures, .samplers=out->samplers};
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        uint32_t table = map->tables[i], slot = map->slots[i];
        if (!table) continue;
        if (table == DOE_COMPILER_TEXT_TABLE_BUFFER) {
            if (out->bindings.buffer_count == DOE_MTL_BUFFER_SLOTS || resolved[i].kind != DOE_BIND_BUFFER)
                return native_failure_status(NATIVE_UNSUPPORTED);
            native_buffer *buffer = doe_resource_buffer_native(resolved[i].resource.buffer.buffer);
            out->buffers[out->bindings.buffer_count++] = (doe_mtl_buffer_binding){slot,
                buffer->handle.metal, resolved[i].resource.buffer.offset, resolved[i].resource.buffer.size};
        } else if (table == DOE_COMPILER_TEXT_TABLE_TEXTURE) {
            if (out->bindings.texture_count == DOE_MTL_TEXTURE_SLOTS ||
                (resolved[i].kind != DOE_BIND_TEXTURE && resolved[i].kind != DOE_BIND_STORAGE_TEXTURE &&
                resolved[i].kind != DOE_BIND_EXTERNAL_TEXTURE))
                return native_failure_status(NATIVE_UNSUPPORTED);
            /* Retained shader lowering samples plane zero. Keep the external
             * object/other planes on the canonical command lease; never invent
             * an adjacent compiler slot or imply YUV conversion. */
            doe_resource_view *resource = resolved[i].kind == DOE_BIND_EXTERNAL_TEXTURE ?
                doe_resource_external_texture_plane(resolved[i].resource.external_texture, 0) : resolved[i].resource.view;
            native_view *view = doe_resource_view_native(resource);
            native_texture *texture = doe_resource_texture_native(doe_resource_view_texture(resource));
            if (doe_mtl_texture_description(texture->metal)->usage & DOE_MTL_TEXTURE_RENDER_TARGET) {
                doe_mtl_error detail = {0};
                doe_mtl_status status = doe_mtl_commands_initialize_attachment(commands, view->metal, &detail);
                if (status != DOE_MTL_OK) return native_failure_metal(status, &detail);
            }
            out->textures[out->bindings.texture_count++] = (doe_mtl_texture_binding){slot,
                view->metal, resolved[i].kind == DOE_BIND_STORAGE_TEXTURE};
        } else if (table == DOE_COMPILER_TEXT_TABLE_SAMPLER) {
            if (out->bindings.sampler_count == DOE_MTL_SAMPLER_SLOTS || resolved[i].kind != DOE_BIND_SAMPLER)
                return native_failure_status(NATIVE_UNSUPPORTED);
            native_sampler *sampler = doe_resource_sampler_native(resolved[i].resource.sampler);
            out->samplers[out->bindings.sampler_count++] = (doe_mtl_sampler_binding){slot, sampler->metal};
        } else return native_failure_status(NATIVE_UNSUPPORTED);
    }
    return native_failure_status(NATIVE_OK);
}
