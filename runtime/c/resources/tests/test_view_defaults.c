/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static doe_resource_texture *create_texture(fixture *f, native_token *token,
    doe_resource_texture_desc desc)
{
    doe_resource_config c = config(f);
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, token), &texture) == DOE_RESOURCE_OK);
    return texture;
}

static void test_layer_defaults(void)
{
    const struct {
        WGPUTextureViewDimension dimension;
        uint32_t base, count, expected_count;
        doe_resource_status status;
    } cases[] = {
        {WGPUTextureViewDimension_Undefined, 0, 0, 12, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_2D, 0, 0, 1, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_2D, 11, UINT32_MAX, 1, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_2D, 12, 0, 0, DOE_RESOURCE_RANGE},
        {WGPUTextureViewDimension_Cube, 0, 0, 6, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_Cube, 6, UINT32_MAX, 6, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_Cube, 7, 0, 0, DOE_RESOURCE_RANGE},
        {WGPUTextureViewDimension_CubeArray, 0, 0, 12, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_CubeArray, 6, UINT32_MAX, 6, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_CubeArray, 1, 0, 0, DOE_RESOURCE_LAYOUT},
        {WGPUTextureViewDimension_2DArray, 7, 0, 5, DOE_RESOURCE_OK},
        {WGPUTextureViewDimension_2D, 0, 2, 0, DOE_RESOURCE_LAYOUT},
        {WGPUTextureViewDimension_Cube, 0, 12, 0, DOE_RESOURCE_LAYOUT},
        {WGPUTextureViewDimension_2DArray, 1, 12, 0, DOE_RESOURCE_RANGE},
        {WGPUTextureViewDimension_2D, UINT32_MAX, 0, 0, DOE_RESOURCE_RANGE}
    };
    fixture f = fresh();
    native_token texture_token = {0};
    doe_resource_texture_desc desc = texture_desc();
    desc.depth_or_layers = 12;
    desc.mip_levels = 4;
    doe_resource_texture *texture = create_texture(&f, &texture_token, desc);
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        doe_resource_view_desc input = {.dimension = cases[i].dimension, .base_layer = cases[i].base,
            .layer_count = cases[i].count, .base_mip = 2};
        doe_resource_view_desc output = {.base_layer = 777};
        size_t allocations = f.allocations;
        assert(doe_resource_view_validate(texture, &input, &output) == cases[i].status);
        assert(f.allocations == allocations);
        native_token view_token = {0};
        doe_resource_view *view = NULL;
        assert(doe_resource_view_create(texture, &input, native(&f, &view_token), &view) == cases[i].status);
        if (cases[i].status == DOE_RESOURCE_OK) {
            assert(output.layer_count == cases[i].expected_count && output.mip_count == 2);
            assert(output.base_layer == cases[i].base);
            assert(output.dimension == (cases[i].dimension ? cases[i].dimension : WGPUTextureViewDimension_2DArray));
            const doe_resource_view_desc *retained = doe_resource_view_description(view);
            assert(retained->layer_count == output.layer_count && retained->base_layer == output.base_layer);
            assert(doe_resource_view_texture(view) == texture);
            doe_resource_view_release(view);
            assert(view_token.releases == 1);
        } else assert(output.base_layer == 777 && !view && !view_token.releases && f.allocations == allocations);
    }
    doe_resource_texture_release(texture);
    assert(texture_token.releases == 1);
    empty(&f);
    for (unsigned dimension = 0; dimension < 2; dimension++) {
        native_token token = {0};
        desc = texture_desc();
        desc.dimension = dimension ? WGPUTextureDimension_3D : WGPUTextureDimension_1D;
        desc.height = dimension ? 8 : 1;
        desc.depth_or_layers = dimension ? 8 : 1;
        texture = create_texture(&f, &token, desc);
        doe_resource_view_desc output;
        assert(doe_resource_view_validate(texture, &(doe_resource_view_desc){0}, &output) == DOE_RESOURCE_OK);
        assert(output.layer_count == 1);
        assert(output.dimension == (dimension ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_1D));
        doe_resource_texture_release(texture);
        empty(&f);
    }
}

static void test_aspect_formats(void)
{
    const struct { WGPUTextureFormat format, depth; bool stencil; } formats[] = {
        {WGPUTextureFormat_Depth16Unorm, WGPUTextureFormat_Depth16Unorm, false},
        {WGPUTextureFormat_Depth24Plus, WGPUTextureFormat_Depth24Plus, false},
        {WGPUTextureFormat_Depth32Float, WGPUTextureFormat_Depth32Float, false},
        {WGPUTextureFormat_Stencil8, WGPUTextureFormat_Undefined, true},
        {WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureFormat_Depth24Plus, true},
        {WGPUTextureFormat_Depth32FloatStencil8, WGPUTextureFormat_Depth32Float, true}
    };
    const WGPUTextureAspect aspects[] = {WGPUTextureAspect_All, WGPUTextureAspect_DepthOnly, WGPUTextureAspect_StencilOnly};
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
        fixture f = fresh();
        native_token texture_token = {0};
        doe_resource_texture_desc desc = texture_desc();
        desc.format = formats[i].format;
        doe_resource_texture *texture = create_texture(&f, &texture_token, desc);
        for (size_t j = 0; j < sizeof(aspects) / sizeof(*aspects); j++) {
            WGPUTextureFormat expected = j == 0 ? desc.format : j == 1 ? formats[i].depth :
                formats[i].stencil ? WGPUTextureFormat_Stencil8 : WGPUTextureFormat_Undefined;
            WGPUTextureFormat effective = WGPUTextureFormat_RGBA8Unorm;
            doe_resource_view_desc input = {.aspect = aspects[j]}, output = {.base_mip = 777};
            if (!expected) {
                assert(doe_resource_format_aspect_format(desc.format, aspects[j], &effective) == DOE_RESOURCE_LAYOUT);
                assert(effective == WGPUTextureFormat_RGBA8Unorm);
                assert(doe_resource_view_validate(texture, &input, &output) == DOE_RESOURCE_ASPECT);
                assert(output.base_mip == 777);
                continue;
            }
            assert(doe_resource_format_aspect_format(desc.format, aspects[j], &effective) == DOE_RESOURCE_OK && effective == expected);
            assert(doe_resource_view_validate(texture, &input, &output) == DOE_RESOURCE_OK && output.format == expected);
            input.format = expected;
            assert(doe_resource_view_validate(texture, &input, &output) == DOE_RESOURCE_OK);
            native_token view_token = {0};
            doe_resource_view *view;
            assert(doe_resource_view_create(texture, &input, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
            assert(doe_resource_view_description(view)->format == expected && doe_resource_view_texture(view) == texture);
            doe_resource_view_release(view);
            assert(view_token.releases == 1);
            if (desc.format != expected) {
                input.format = desc.format;
                output.base_mip = 777;
                assert(doe_resource_view_validate(texture, &input, &output) == DOE_RESOURCE_LAYOUT && output.base_mip == 777);
            }
        }
        doe_resource_texture_release(texture);
        assert(texture_token.releases == 1);
        empty(&f);
    }
    WGPUTextureFormat output = WGPUTextureFormat_RGBA8Unorm;
    assert(doe_resource_format_aspect_format(WGPUTextureFormat_Undefined, WGPUTextureAspect_All, &output) == DOE_RESOURCE_UNSUPPORTED);
    assert(doe_resource_format_aspect_format(WGPUTextureFormat_RGBA8Unorm, WGPUTextureAspect_DepthOnly, &output) == DOE_RESOURCE_LAYOUT);
    assert(doe_resource_format_aspect_format(WGPUTextureFormat_Stencil8, WGPUTextureAspect_Undefined, &output) == DOE_RESOURCE_LAYOUT);
    assert(output == WGPUTextureFormat_RGBA8Unorm);
    assert(doe_resource_format_aspect_format(WGPUTextureFormat_Stencil8, WGPUTextureAspect_All, NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
}

static void test_render_all_actual_aspects(void)
{
    const WGPUTextureFormat formats[] = {WGPUTextureFormat_Depth32Float,
        WGPUTextureFormat_Stencil8, WGPUTextureFormat_Depth32FloatStencil8};
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
        fixture f = fresh();
        doe_resource_config c = config(&f);
        native_token tokens[4] = {{0}};
        doe_resource_texture_desc desc = texture_desc();
        desc.usage = WGPUTextureUsage_RenderAttachment;
        doe_resource_texture *color = create_texture(&f, &tokens[0], desc);
        desc.format = formats[i];
        doe_resource_texture *depth = create_texture(&f, &tokens[1], desc);
        doe_resource_view *color_view, *depth_view;
        assert(doe_resource_view_create(color, &(doe_resource_view_desc){0}, native(&f, &tokens[2]), &color_view) == DOE_RESOURCE_OK);
        doe_resource_view_desc depth_desc = {.aspect = i == 1 ? WGPUTextureAspect_StencilOnly : WGPUTextureAspect_DepthOnly};
        assert(doe_resource_view_create(depth, &depth_desc, native(&f, &tokens[3]), &depth_view) == DOE_RESOURCE_OK);
        doe_resource_render_targets targets = {.color = color_view, .depth_stencil = depth_view,
            .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
        if (i == 1) { targets.stencil_load = WGPULoadOp_Clear; targets.stencil_store = WGPUStoreOp_Store; }
        else { targets.depth_load = WGPULoadOp_Clear; targets.depth_store = WGPUStoreOp_Store; }
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass = NULL;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        doe_resource_status expected = i == 2 ? DOE_RESOURCE_LAYOUT : DOE_RESOURCE_OK;
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == expected);
        if (pass) {
            assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
            doe_resource_render_pass_release(pass);
            doe_resource_commands *commands;
            assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
            doe_resource_commands_release(commands);
        }
        doe_resource_encoder_release(encoder);
        doe_resource_texture_release(color);
        doe_resource_texture_release(depth);
        doe_resource_view_release(color_view);
        doe_resource_view_release(depth_view);
        for (size_t j = 0; j < 4; j++) assert(tokens[j].releases == 1);
        empty(&f);
    }
}

int main(void)
{
    test_layer_defaults();
    test_aspect_formats();
    test_render_all_actual_aspects();
    puts("view defaults owner: 3 host suites passed; no new native view execution claimed");
    return 0;
}
