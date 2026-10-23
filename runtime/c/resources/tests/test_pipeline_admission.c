/* SPDX-License-Identifier: MIT
 * Setter validation is not deferred until a later draw or overwritten state. */
#include "fixture.h"
#include "../src/internal.h"

static void test_binding_entry_constraints(void)
{
    const WGPUShaderStage stages[] = {0, WGPUShaderStage_Vertex, WGPUShaderStage_Fragment,
        WGPUShaderStage_Compute, WGPUShaderStage_Vertex | WGPUShaderStage_Fragment};
    for (unsigned automatic = 0; automatic < 2; automatic++) {
        for (unsigned kind = 0; kind < 3; kind++) {
            for (unsigned type = 0; type < 3; type++) {
                for (size_t stage = 0; stage < sizeof(stages) / sizeof(*stages); stage++) {
                    fixture f = fresh();
                    doe_resource_config c = config(&f);
                    doe_resource_layout_entry entries[2] = {
                        {.kind = DOE_BIND_SAMPLER, .sampler_type = WGPUSamplerBindingType_Filtering},
                        {.binding = 1, .visibility = stages[stage]}
                    };
                    bool rejected = false;
                    if (kind == 0) {
                        const WGPUBufferBindingType types[] = {WGPUBufferBindingType_Uniform,
                            WGPUBufferBindingType_ReadOnlyStorage, WGPUBufferBindingType_Storage};
                        entries[1].kind = DOE_BIND_BUFFER;
                        entries[1].buffer_type = types[type];
                        rejected = type == 2 && (stages[stage] & WGPUShaderStage_Vertex);
                    } else if (kind == 1) {
                        const WGPUStorageTextureAccess types[] = {WGPUStorageTextureAccess_ReadOnly,
                            WGPUStorageTextureAccess_WriteOnly, WGPUStorageTextureAccess_ReadWrite};
                        entries[1].kind = DOE_BIND_STORAGE_TEXTURE;
                        entries[1].storage_access = types[type];
                        entries[1].storage_format = WGPUTextureFormat_R32Float; /* Read-write capable. */
                        rejected = type != 0 && (stages[stage] & WGPUShaderStage_Vertex);
                    } else {
                        const WGPUTextureSampleType types[] = {WGPUTextureSampleType_UnfilterableFloat,
                            WGPUTextureSampleType_Depth, WGPUTextureSampleType_Float};
                        entries[1].kind = DOE_BIND_TEXTURE;
                        entries[1].sample_type = types[type];
                        entries[1].multisampled = true;
                        rejected = type == 2;
                    }
                    doe_resource_layout *layout = (void *)(uintptr_t)1;
                    doe_resource_status status = doe_resource_layout_create(&c, entries, 2, automatic != 0, &layout);
                    if (rejected) {
                        assert(status == DOE_RESOURCE_LAYOUT && layout == (void *)(uintptr_t)1);
                        assert(!f.allocations && !f.device_references);
                    } else {
                        assert(status == DOE_RESOURCE_OK);
                        doe_resource_layout_release(layout);
                    }
                    empty(&f);
                }
            }
        }
    }
}

static doe_resource_view *attachment(fixture *f, native_token tokens[2], WGPUTextureFormat format)
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc td = texture_desc();
    td.format = format;
    td.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &td, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0}, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}

static void check_setter(unsigned scenario, bool bundled)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token pipeline_tokens[2] = {{0}}, color_tokens[2] = {{0}}, depth_tokens[2] = {{0}};
    doe_resource_pipeline_layout *layout;
    doe_resource_render_pipeline *baseline, *candidate;
    doe_resource_render_pipeline_desc pd = {.color_format = WGPUTextureFormat_RGBA8Unorm,
        .depth_stencil_format = WGPUTextureFormat_Depth32FloatStencil8, .samples = 1};
    doe_resource_render_bundle_desc signature = {.color_format = pd.color_format,
        .depth_stencil_format = pd.depth_stencil_format, .samples = 1,
        .depth_read_only = true, .stencil_read_only = true};
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pipeline_create(&c, layout, &pd, native(&f, &pipeline_tokens[0]), &baseline) == DOE_RESOURCE_OK);
    if (scenario == 1) pd.color_format = WGPUTextureFormat_RGBA8Uint;
    if (scenario == 2) pd.samples = 4;
    if (scenario == 3) pd.depth_write = true;
    if (scenario == 4) pd.stencil_write = true;
    if (scenario == 5) pd.depth_stencil_format = WGPUTextureFormat_Depth32Float;
    assert(doe_resource_render_pipeline_create(&c, layout, &pd, native(&f, &pipeline_tokens[1]), &candidate) == DOE_RESOURCE_OK);
    doe_resource_status expected = scenario ? DOE_RESOURCE_LAYOUT : DOE_RESOURCE_OK;
    size_t allocations;
    if (bundled) {
        doe_resource_bundle_encoder *builder;
        assert(doe_resource_bundle_encoder_create(&c, &signature, &builder) == DOE_RESOURCE_OK);
        assert(doe_resource_bundle_encoder_set_pipeline(builder, baseline) == DOE_RESOURCE_OK);
        allocations = f.allocations;
        assert(doe_resource_bundle_encoder_set_pipeline(builder, candidate) == expected);
        assert(f.allocations == allocations);
        assert(atomic_load(&baseline->base.references) == (scenario ? 2 : 1));
        assert(atomic_load(&candidate->base.references) == (scenario ? 1 : 2));
        if (scenario) assert(doe_resource_bundle_encoder_set_pipeline(builder, baseline) == expected);
        doe_resource_render_bundle *result = NULL;
        assert(doe_resource_bundle_encoder_finish(builder, &result) == expected);
        doe_resource_bundle_encoder_release(builder);
        doe_resource_render_bundle_release(result);
    } else {
        doe_resource_view *color = attachment(&f, color_tokens, signature.color_format);
        doe_resource_view *depth = attachment(&f, depth_tokens, signature.depth_stencil_format);
        doe_resource_render_targets targets = {.color = color, .depth_stencil = depth,
            .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store,
            .depth_read_only = true, .stencil_read_only = true};
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == DOE_RESOURCE_OK);
        assert(doe_resource_render_pass_set_pipeline(pass, baseline) == DOE_RESOURCE_OK);
        allocations = f.allocations;
        assert(doe_resource_render_pass_set_pipeline(pass, candidate) == expected);
        assert(f.allocations == allocations && encoder->count == 1);
        assert(pass->state.pipeline == (scenario ? baseline : candidate));
        assert(atomic_load(&candidate->base.references) == (scenario ? 1 : 2));
        if (scenario) assert(doe_resource_render_pass_set_pipeline(pass, baseline) == expected);
        assert(doe_resource_render_pass_end(pass) == expected);
        doe_resource_commands *commands = NULL;
        assert(doe_resource_encoder_finish(encoder, &commands) == expected);
        doe_resource_render_pass_release(pass);
        doe_resource_encoder_release(encoder);
        doe_resource_view_release(color);
        doe_resource_view_release(depth);
        doe_resource_commands_release(commands);
        assert(color_tokens[0].releases == 1 && color_tokens[1].releases == 1);
        assert(depth_tokens[0].releases == 1 && depth_tokens[1].releases == 1);
    }
    doe_resource_render_pipeline_release(candidate);
    doe_resource_render_pipeline_release(baseline);
    doe_resource_pipeline_layout_release(layout);
    assert(pipeline_tokens[0].releases == 1 && pipeline_tokens[1].releases == 1);
    empty(&f);
}

int main(void)
{
    test_binding_entry_constraints();
    for (unsigned scenario = 0; scenario < 6; scenario++) {
        check_setter(scenario, false);
        check_setter(scenario, true);
    }
    puts("pipeline admission: 2 host suites passed; no native shader execution claimed");
    return 0;
}
