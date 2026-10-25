/* SPDX-License-Identifier: MIT
 * Invalid objects of a device that is no longer alive: created only then,
 * descriptors kept for getters, every operation UNAVAILABLE, allocation
 * failure NULL, release frees everything. Host ownership only. */
#include "fixture.h"

int main(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    assert(!doe_resource_buffer_lost(&c, NULL, (doe_resource_native){0})); /* Live device: refused. */
    f.alive = false;
    const doe_resource_buffer_desc buffer_desc = {256, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, true};
    native_token token = {0};
    doe_resource_buffer *buffer = doe_resource_buffer_lost(&c, &buffer_desc, (doe_resource_native){&token, &f, native_release});
    assert(buffer && doe_resource_buffer_description(buffer)->size == 256);
    assert(doe_resource_buffer_map_state(buffer) == WGPUBufferMapState_Unmapped); /* Never mapped at creation. */
    uint64_t generation;
    assert(doe_resource_buffer_map_begin(buffer, WGPUMapMode_Read, 0, 64, &generation) == DOE_RESOURCE_UNAVAILABLE);
    const doe_resource_texture_desc texture_desc = {.dimension = WGPUTextureDimension_2D,
        .format = WGPUTextureFormat_RGBA8Unorm, .width = 8, .height = 4, .depth_or_layers = 1, .mip_levels = 1, .samples = 1};
    doe_resource_texture *texture = doe_resource_texture_lost(&c, &texture_desc);
    assert(texture && doe_resource_texture_description(texture)->width == 8);
    doe_resource_view *view = doe_resource_view_lost(texture, NULL);
    doe_resource_encoder *encoder = doe_resource_encoder_lost(&c);
    assert(view && encoder);
    doe_resource_commands *commands = NULL;
    assert(doe_resource_encoder_finish(encoder, &commands) != DOE_RESOURCE_OK && !commands);
    doe_resource_query_set *queries = doe_resource_query_set_lost(&c, WGPUQueryType_Occlusion, 4);
    doe_resource_shader *shader = doe_resource_shader_lost(&c);
    doe_resource_sampler *sampler = doe_resource_sampler_lost(&c);
    doe_resource_layout *layout = doe_resource_layout_lost(&c);
    doe_resource_pipeline_layout *pipeline_layout = doe_resource_pipeline_layout_lost(&c);
    doe_resource_bind_group *group = doe_resource_bind_group_lost(&c);
    doe_resource_pipeline *pipeline = doe_resource_pipeline_lost(&c);
    doe_resource_render_pipeline *render = doe_resource_render_pipeline_lost(&c);
    assert(queries && shader && sampler && layout && pipeline_layout && group && pipeline && render);
    f.fail_at = f.allocations; /* Allocation failure returns NULL. */
    assert(!doe_resource_shader_lost(&c));
    f.fail_at = SIZE_MAX;
    doe_resource_render_pipeline_release(render);
    doe_resource_pipeline_release(pipeline);
    doe_resource_bind_group_release(group);
    doe_resource_pipeline_layout_release(pipeline_layout);
    doe_resource_layout_release(layout);
    doe_resource_sampler_release(sampler);
    doe_resource_shader_release(shader);
    doe_resource_query_set_release(queries);
    doe_resource_encoder_release(encoder);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    doe_resource_buffer_release(buffer);
    assert(token.releases == 1);
    empty(&f);
    puts("PASS lost-device invalid objects: refused while alive, descriptors kept, uses unavailable, allocation failure NULL, complete release");
    return 0;
}
