/* SPDX-License-Identifier: MIT */
#include "fixture.h"

static void check_sample_class(WGPUTextureFormat format, WGPUTextureAspect aspect,
    WGPUTextureSampleType expected, bool optional_filter)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token image_token = {0}, view_token = {0};
    doe_resource_texture_desc desc = {.dimension = WGPUTextureDimension_2D,
        .format = format, .width = 4, .height = 4, .depth_or_layers = 1,
        .mip_levels = 1, .samples = 1, .usage = WGPUTextureUsage_TextureBinding};
    doe_resource_texture_limits texture_limits = {64, 64, 64, 64};
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &desc, &texture_limits,
        native(&f, &image_token), &texture) == DOE_RESOURCE_OK);
    doe_resource_view_desc view_desc = {.aspect = aspect};
    assert(doe_resource_view_create(texture, &view_desc,
        native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.kind = DOE_BIND_TEXTURE, .resource.view = view};
    doe_resource_layout_entry entry = {.visibility = WGPUShaderStage_Compute,
        .kind = DOE_BIND_TEXTURE, .sample_type = expected};
    doe_resource_binding_limits limits = {16, 16, 1024, 1024};
    doe_resource_layout *layout;
    doe_resource_bind_group *group;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    assert(doe_resource_bind_group_create(layout, &binding, 1, &limits, &group) == DOE_RESOURCE_OK);
    doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    entry.sample_type = WGPUTextureSampleType_Float;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    group = (void *)(uintptr_t)1;
    size_t allocations = f.allocations;
    assert(doe_resource_bind_group_create(layout, &binding, 1, &limits, &group) == DOE_RESOURCE_LAYOUT);
    assert(group == (void *)(uintptr_t)1 && f.allocations == allocations);
    doe_resource_binding_features features = {.float32_filterable = true};
    doe_resource_status status = doe_resource_bind_group_create_with_features(layout, &binding, 1,
        &limits, &features, &group);
    if (optional_filter) {
        assert(status == DOE_RESOURCE_OK);
        features.float32_filterable = false;
        doe_resource_binding resolved;
        assert(doe_resource_bind_group_resolve(group, NULL, 0, &resolved, 1) == DOE_RESOURCE_OK);
        assert(resolved.resource.view == view);
        doe_resource_bind_group_release(group);
    } else {
        assert(status == DOE_RESOURCE_LAYOUT && group == (void *)(uintptr_t)1);
    }
    doe_resource_layout_release(layout);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    assert(image_token.releases == 1 && view_token.releases == 1);
    empty(&f);
}

static void check_sampler_filter(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_binding_limits limits = {16, 16, 1024, 1024};
    doe_resource_layout_entry entry = {.visibility = WGPUShaderStage_Compute,
        .kind = DOE_BIND_SAMPLER, .sampler_type = WGPUSamplerBindingType_NonFiltering};
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    for (unsigned mode = 0; mode < 4; mode++) {
        native_token token = {0};
        WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        if (mode == 1) desc.minFilter = WGPUFilterMode_Linear;
        if (mode == 2) desc.magFilter = WGPUFilterMode_Linear;
        if (mode == 3) desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        doe_resource_sampler *sampler;
        assert(doe_resource_sampler_create(&c, &desc, native(&f, &token), &sampler) == DOE_RESOURCE_OK);
        const WGPUSamplerDescriptor *normalized = doe_resource_sampler_description(sampler);
        assert(normalized && normalized->minFilter == (mode == 1 ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest));
        assert(!normalized->label.data);
        doe_resource_binding binding = {.kind = DOE_BIND_SAMPLER, .resource.sampler = sampler};
        doe_resource_bind_group *group = NULL;
        doe_resource_status status = doe_resource_bind_group_create(layout, &binding, 1, &limits, &group);
        assert(status == (mode ? DOE_RESOURCE_LAYOUT : DOE_RESOURCE_OK));
        if (group) doe_resource_bind_group_release(group);
        doe_resource_sampler_release(sampler);
        assert(token.releases == 1);
    }
    assert(!doe_resource_sampler_description(NULL));
    doe_resource_layout_release(layout);
    empty(&f);
}

int main(void)
{
    check_sample_class(WGPUTextureFormat_R32Float, WGPUTextureAspect_All,
        WGPUTextureSampleType_UnfilterableFloat, true);
    check_sample_class(WGPUTextureFormat_RG32Float, WGPUTextureAspect_All,
        WGPUTextureSampleType_UnfilterableFloat, true);
    check_sample_class(WGPUTextureFormat_RGBA32Float, WGPUTextureAspect_All,
        WGPUTextureSampleType_UnfilterableFloat, true);
    check_sample_class(WGPUTextureFormat_Stencil8, WGPUTextureAspect_All,
        WGPUTextureSampleType_Uint, false);
    check_sample_class(WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureAspect_StencilOnly,
        WGPUTextureSampleType_Uint, false);
    check_sampler_filter();
    puts("binding feature owner: 6 host suites passed; no texture execution claimed");
    return 0;
}
