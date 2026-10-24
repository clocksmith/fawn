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

static void check_format_class(void)
{
    static const struct {
        WGPUTextureFormat format;
        WGPUTextureAspect aspect;
        WGPUTextureSampleType sample;
    } cases[] = {
        {WGPUTextureFormat_RGBA8Unorm, WGPUTextureAspect_All, WGPUTextureSampleType_Float},
        {WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureAspect_All, WGPUTextureSampleType_Float},
        {WGPUTextureFormat_RGBA16Unorm, WGPUTextureAspect_All, WGPUTextureSampleType_Float},
        {WGPUTextureFormat_RGBA16Float, WGPUTextureAspect_All, WGPUTextureSampleType_Float},
        {WGPUTextureFormat_RGBA16Uint, WGPUTextureAspect_All, WGPUTextureSampleType_Uint},
        {WGPUTextureFormat_RGBA16Sint, WGPUTextureAspect_All, WGPUTextureSampleType_Sint},
        {WGPUTextureFormat_RGB10A2Uint, WGPUTextureAspect_All, WGPUTextureSampleType_Uint},
        {WGPUTextureFormat_RGBA32Uint, WGPUTextureAspect_All, WGPUTextureSampleType_Uint},
        {WGPUTextureFormat_RGBA32Sint, WGPUTextureAspect_All, WGPUTextureSampleType_Sint},
        {WGPUTextureFormat_RGBA32Float, WGPUTextureAspect_All, WGPUTextureSampleType_UnfilterableFloat},
        {WGPUTextureFormat_Depth32Float, WGPUTextureAspect_DepthOnly, WGPUTextureSampleType_Depth},
        {WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureAspect_DepthOnly, WGPUTextureSampleType_Depth},
        {WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureAspect_StencilOnly, WGPUTextureSampleType_Uint},
        {WGPUTextureFormat_Depth32FloatStencil8, WGPUTextureAspect_StencilOnly, WGPUTextureSampleType_Uint},
        {WGPUTextureFormat_Stencil8, WGPUTextureAspect_All, WGPUTextureSampleType_Uint},
        {WGPUTextureFormat_BC1RGBAUnorm, WGPUTextureAspect_All, WGPUTextureSampleType_Float}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        WGPUTextureSampleType sample = WGPUTextureSampleType_Undefined;
        assert(doe_resource_format_sample_type(cases[i].format, cases[i].aspect, &sample) == DOE_RESOURCE_OK);
        assert(sample == cases[i].sample);
    }
    WGPUTextureSampleType sample = WGPUTextureSampleType_Sint;
    assert(doe_resource_format_sample_type(WGPUTextureFormat_Undefined,
        WGPUTextureAspect_All, &sample) == DOE_RESOURCE_UNSUPPORTED);
    assert(sample == WGPUTextureSampleType_Sint);
    assert(doe_resource_format_sample_type((WGPUTextureFormat)UINT32_C(0x7ffffffe),
        WGPUTextureAspect_All, &sample) == DOE_RESOURCE_UNSUPPORTED);
    assert(sample == WGPUTextureSampleType_Sint);
    assert(doe_resource_format_sample_type(WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureAspect_StencilOnly, &sample) == DOE_RESOURCE_LAYOUT);
    assert(sample == WGPUTextureSampleType_Sint);
    assert(doe_resource_format_sample_type(WGPUTextureFormat_Stencil8,
        WGPUTextureAspect_DepthOnly, &sample) == DOE_RESOURCE_LAYOUT);
    assert(sample == WGPUTextureSampleType_Sint);
    assert(doe_resource_format_sample_type(WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureAspect_Undefined, &sample) == DOE_RESOURCE_LAYOUT);
    assert(sample == WGPUTextureSampleType_Sint);
    assert(doe_resource_format_sample_type(WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureAspect_All, NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
}

static void check_color_components(void)
{
    static const struct { WGPUTextureFormat format; uint32_t components; } cases[] = {
        {WGPUTextureFormat_R8Uint, 1}, {WGPUTextureFormat_R16Float, 1}, {WGPUTextureFormat_R32Float, 1},
        {WGPUTextureFormat_RG8Unorm, 2}, {WGPUTextureFormat_RG16Unorm, 2}, {WGPUTextureFormat_RG32Sint, 2},
        {WGPUTextureFormat_RGB9E5Ufloat, 3}, {WGPUTextureFormat_RG11B10Ufloat, 3},
        {WGPUTextureFormat_RGBA8Unorm, 4}, {WGPUTextureFormat_BGRA8UnormSrgb, 4},
        {WGPUTextureFormat_RGB10A2Uint, 4}, {WGPUTextureFormat_RGBA16Uint, 4}, {WGPUTextureFormat_RGBA32Float, 4},
        {WGPUTextureFormat_BC4RSnorm, 1}, {WGPUTextureFormat_BC5RGUnorm, 2},
        {WGPUTextureFormat_BC6HRGBUfloat, 3}, {WGPUTextureFormat_BC1RGBAUnorm, 4},
        {WGPUTextureFormat_EACR11Unorm, 1}, {WGPUTextureFormat_EACRG11Snorm, 2},
        {WGPUTextureFormat_ETC2RGB8UnormSrgb, 3}, {WGPUTextureFormat_ETC2RGB8A1Unorm, 4},
        {WGPUTextureFormat_ASTC12x12UnormSrgb, 4}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t components = 0;
        assert(doe_resource_format_color_components(cases[i].format, &components) == DOE_RESOURCE_OK);
        assert(components == cases[i].components);
    }
    uint32_t components = 99;
    assert(doe_resource_format_color_components(WGPUTextureFormat_Undefined, &components) == DOE_RESOURCE_UNSUPPORTED);
    assert(components == 99);
    assert(doe_resource_format_color_components(WGPUTextureFormat_Depth24PlusStencil8, &components) == DOE_RESOURCE_LAYOUT);
    assert(components == 99);
    assert(doe_resource_format_color_components(WGPUTextureFormat_Stencil8, &components) == DOE_RESOURCE_LAYOUT);
    assert(components == 99);
    assert(doe_resource_format_color_components(WGPUTextureFormat_RGBA8Unorm, NULL) == DOE_RESOURCE_INVALID_ARGUMENT);
}

/* A texture view binds to an externalTexture entry as a sampled texture when
 * it is a 2d, single-mip, single-layer TEXTURE_BINDING view of rgba8unorm,
 * bgra8unorm or rgba16float. */
static doe_resource_status external_view_status(WGPUTextureFormat format, uint32_t mips,
    WGPUTextureUsage usage)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token image_token = {0}, view_token = {0};
    doe_resource_texture_desc desc = {.dimension = WGPUTextureDimension_2D,
        .format = format, .width = 4, .height = 4, .depth_or_layers = 1,
        .mip_levels = mips, .samples = 1, .usage = usage};
    doe_resource_texture_limits texture_limits = {64, 64, 64, 64};
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &desc, &texture_limits, native(&f, &image_token), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0}, native(&f, &view_token), &view) == DOE_RESOURCE_OK);
    doe_resource_layout_entry entry = {.visibility = WGPUShaderStage_Fragment, .kind = DOE_BIND_EXTERNAL_TEXTURE};
    doe_resource_layout *layout;
    assert(doe_resource_layout_create(&c, &entry, 1, false, &layout) == DOE_RESOURCE_OK);
    doe_resource_binding binding = {.kind = DOE_BIND_TEXTURE, .resource.view = view};
    doe_resource_binding_limits limits = {16, 16, 1024, 1024};
    doe_resource_bind_group *group = NULL;
    doe_resource_status status = doe_resource_bind_group_create(layout, &binding, 1, &limits, &group);
    if (group) doe_resource_bind_group_release(group);
    doe_resource_layout_release(layout);
    doe_resource_view_release(view);
    doe_resource_texture_release(texture);
    assert(image_token.releases == 1 && view_token.releases == 1);
    empty(&f);
    return status;
}

static void check_external_view(void)
{
    assert(external_view_status(WGPUTextureFormat_RGBA8Unorm, 1, WGPUTextureUsage_TextureBinding) == DOE_RESOURCE_OK);
    assert(external_view_status(WGPUTextureFormat_BGRA8Unorm, 1, WGPUTextureUsage_TextureBinding) == DOE_RESOURCE_OK);
    assert(external_view_status(WGPUTextureFormat_RGBA16Float, 1, WGPUTextureUsage_TextureBinding) == DOE_RESOURCE_OK);
    assert(external_view_status(WGPUTextureFormat_R8Unorm, 1, WGPUTextureUsage_TextureBinding) == DOE_RESOURCE_LAYOUT);
    assert(external_view_status(WGPUTextureFormat_RGBA8Unorm, 2, WGPUTextureUsage_TextureBinding) == DOE_RESOURCE_LAYOUT);
    assert(external_view_status(WGPUTextureFormat_RGBA8Unorm, 1, WGPUTextureUsage_CopyDst) == DOE_RESOURCE_USAGE);
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
    check_format_class();
    check_color_components();
    check_external_view();
    puts("binding feature owner: 9 host suites passed; no texture execution claimed");
    return 0;
}
