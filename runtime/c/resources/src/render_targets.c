/* SPDX-License-Identifier: MIT
 * The one owner of color-target layout rules: slot normalization, renderable
 * byte costs, the per-sample byte limit and layout equality. Backend format
 * renderability and features remain separate native admission.
 * https://gpuweb.github.io/gpuweb/#plain-color-formats (render target pixel
 * byte cost and component alignment) and
 * https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-color-attachment-bytes-per-sample */
#include "internal.h"
#include <string.h>

/* WebGPU color-target capabilities (plain color formats table): byte cost,
 * alignment, multisampling and resolve. Optional normalized targets have
 * a separate requested-feature gate; rgb9e5/compressed are not color targets.
 * rg11b10ufloat is listed; its feature gate
 * is native. https://gpuweb.github.io/gpuweb/#plain-color-formats */
enum { MS = 1, RESOLVE = 2 };
static bool target_caps(WGPUTextureFormat format, uint32_t *cost, uint32_t *alignment, unsigned *caps)
{
    uint32_t c, a;
    unsigned flags;
    switch (format) {
    case WGPUTextureFormat_R8Snorm: c=1; a=1; flags=MS|RESOLVE; break;
    case WGPUTextureFormat_RG8Snorm: c=2; a=1; flags=MS|RESOLVE; break;
    case WGPUTextureFormat_RGBA8Snorm: c=4; a=1; flags=MS|RESOLVE; break;
    case WGPUTextureFormat_R16Unorm: case WGPUTextureFormat_R16Snorm: c=2; a=2; flags=MS|RESOLVE; break;
    case WGPUTextureFormat_RG16Unorm: case WGPUTextureFormat_RG16Snorm: c=4; a=2; flags=MS|RESOLVE; break;
    case WGPUTextureFormat_RGBA16Unorm: case WGPUTextureFormat_RGBA16Snorm: c=8; a=2; flags=MS|RESOLVE; break;
    case WGPUTextureFormat_R8Unorm: c = 1; a = 1; flags = MS | RESOLVE; break;
    case WGPUTextureFormat_R8Uint: case WGPUTextureFormat_R8Sint: c = 1; a = 1; flags = MS; break;
    case WGPUTextureFormat_RG8Unorm: c = 2; a = 1; flags = MS | RESOLVE; break;
    case WGPUTextureFormat_RG8Uint: case WGPUTextureFormat_RG8Sint: c = 2; a = 1; flags = MS; break;
    case WGPUTextureFormat_RGBA8Uint: case WGPUTextureFormat_RGBA8Sint: c = 4; a = 1; flags = MS; break;
    case WGPUTextureFormat_RGBA8Unorm: case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_BGRA8Unorm: case WGPUTextureFormat_BGRA8UnormSrgb:
        c = 8; a = 1; flags = MS | RESOLVE; break;
    case WGPUTextureFormat_R16Uint: case WGPUTextureFormat_R16Sint: c = 2; a = 2; flags = MS; break;
    case WGPUTextureFormat_R16Float: c = 2; a = 2; flags = MS | RESOLVE; break;
    case WGPUTextureFormat_RG16Uint: case WGPUTextureFormat_RG16Sint: c = 4; a = 2; flags = MS; break;
    case WGPUTextureFormat_RG16Float: c = 4; a = 2; flags = MS | RESOLVE; break;
    case WGPUTextureFormat_RGBA16Uint: case WGPUTextureFormat_RGBA16Sint: c = 8; a = 2; flags = MS; break;
    case WGPUTextureFormat_RGBA16Float: c = 8; a = 2; flags = MS | RESOLVE; break;
    case WGPUTextureFormat_R32Uint: case WGPUTextureFormat_R32Sint: c = 4; a = 4; flags = 0; break;
    case WGPUTextureFormat_R32Float: c = 4; a = 4; flags = MS; break;
    case WGPUTextureFormat_RG32Uint: case WGPUTextureFormat_RG32Sint: case WGPUTextureFormat_RG32Float:
        c = 8; a = 4; flags = 0; break;
    case WGPUTextureFormat_RGB10A2Uint: c = 8; a = 4; flags = MS; break;
    case WGPUTextureFormat_RGB10A2Unorm: case WGPUTextureFormat_RG11B10Ufloat: c = 8; a = 4; flags = MS | RESOLVE; break;
    case WGPUTextureFormat_RGBA32Uint: case WGPUTextureFormat_RGBA32Sint: case WGPUTextureFormat_RGBA32Float:
        c = 16; a = 4; flags = 0; break;
    default:
        return false;
    }
    if (cost) *cost = c;
    if (alignment) *alignment = a;
    if (caps) *caps = flags;
    return true;
}
static bool target_cost(WGPUTextureFormat format, uint32_t *cost, uint32_t *alignment)
{
    return target_caps(format, cost, alignment, NULL);
}

bool doe_resource_format_color_renderable(WGPUTextureFormat format, uint32_t features)
{
    switch (format) {
    case WGPUTextureFormat_R8Snorm: case WGPUTextureFormat_RG8Snorm: case WGPUTextureFormat_RGBA8Snorm:
    case WGPUTextureFormat_R16Unorm: case WGPUTextureFormat_R16Snorm:
    case WGPUTextureFormat_RG16Unorm: case WGPUTextureFormat_RG16Snorm:
    case WGPUTextureFormat_RGBA16Unorm: case WGPUTextureFormat_RGBA16Snorm:
        if (!(features & (DOE_RESOURCE_FORMAT_TIER1 | DOE_RESOURCE_FORMAT_TIER2))) return false;
        break;
    default: break;
    }
    return target_caps(format, NULL, NULL, NULL);
}
bool resource_format_color_renderable(WGPUTextureFormat format)
{
    return doe_resource_format_color_renderable(format, 0);
}

/* Depth/stencil formats all multisample; color formats per the table. */
bool resource_format_multisample(WGPUTextureFormat format)
{
    unsigned caps = 0;
    return resource_format_depth(format) || (target_caps(format, NULL, NULL, &caps) && (caps & MS));
}

bool resource_format_resolvable(WGPUTextureFormat format)
{
    unsigned caps = 0;
    return target_caps(format, NULL, NULL, &caps) && (caps & RESOLVE);
}

uint32_t resource_max_color_attachments(const doe_resource_config *config)
{
    uint32_t limit = config->max_color_attachments ? config->max_color_attachments : 8;
    return limit < DOE_RESOURCE_MAX_COLOR_ATTACHMENTS ? limit : DOE_RESOURCE_MAX_COLOR_ATTACHMENTS;
}

doe_resource_status resource_color_layout_validate(const doe_resource_config *config,
    const WGPUTextureFormat *formats, uint32_t count, WGPUTextureFormat depth_stencil, uint32_t samples)
{
    if (count > resource_max_color_attachments(config)) return DOE_RESOURCE_RANGE;
    uint32_t limit = config->max_color_attachment_bytes_per_sample ?
        config->max_color_attachment_bytes_per_sample : 32;
    uint32_t total = 0;
    bool any = false;
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i] == WGPUTextureFormat_Undefined) continue;
        uint32_t cost, alignment;
        if (!doe_resource_format_color_renderable(formats[i], config->format_features) ||
            !target_cost(formats[i], &cost, &alignment)) return DOE_RESOURCE_LAYOUT;
        /* total stays at most limit + 16, so neither step can wrap. */
        total = (total + alignment - 1) / alignment * alignment + cost;
        if (total > limit) return DOE_RESOURCE_RANGE;
        any = true;
    }
    if (!any && !depth_stencil) return DOE_RESOURCE_LAYOUT;
    if (depth_stencil && !resource_format_depth(depth_stencil)) return DOE_RESOURCE_LAYOUT;
    if (samples != 1 && samples != 4) return DOE_RESOURCE_UNSUPPORTED;
    /* WebGPU checks multisample support when a texture is created, not for a
     * pipeline or bundle layout: a layout whose format cannot multisample is
     * valid, and no attachment matching it can exist. */
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_color_formats_normalize(WGPUTextureFormat legacy, uint32_t count,
    const WGPUTextureFormat *formats, uint32_t *out_count, WGPUTextureFormat out[DOE_RESOURCE_MAX_COLOR_ATTACHMENTS])
{
    if (count && legacy) return DOE_RESOURCE_LAYOUT;
    if (count > DOE_RESOURCE_MAX_COLOR_ATTACHMENTS) return DOE_RESOURCE_RANGE;
    WGPUTextureFormat normalized[DOE_RESOURCE_MAX_COLOR_ATTACHMENTS] = {0};
    if (legacy) normalized[0] = legacy;
    for (uint32_t i = 0; i < count; i++) normalized[i] = formats[i];
    *out_count = legacy ? 1 : count;
    memcpy(out, normalized, sizeof(normalized));
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_render_targets_normalize(const doe_resource_render_targets *in,
    doe_resource_render_targets *out)
{
    doe_resource_render_targets normalized = *in;
    bool legacy = in->color || in->resolve_target;
    if (!in->color && (in->resolve_target || in->color_load || in->color_store)) return DOE_RESOURCE_LAYOUT;
    if (in->color_count > DOE_RESOURCE_MAX_COLOR_ATTACHMENTS) return DOE_RESOURCE_RANGE;
    if (legacy) {
        if (in->color_count) return DOE_RESOURCE_LAYOUT;
        memset(normalized.colors, 0, sizeof(normalized.colors));
        normalized.color_count = 1;
        normalized.colors[0] = (doe_resource_color_attachment){.view=in->color, .resolve_target=in->resolve_target,
            .load=in->color_load, .store=in->color_store,
            .clear={in->clear_color[0], in->clear_color[1], in->clear_color[2], in->clear_color[3]}};
    }
    for (uint32_t i = normalized.color_count; i < DOE_RESOURCE_MAX_COLOR_ATTACHMENTS; i++)
        normalized.colors[i] = (doe_resource_color_attachment){0};
    normalized.color = normalized.resolve_target = NULL;
    normalized.color_load = WGPULoadOp_Undefined;
    normalized.color_store = WGPUStoreOp_Undefined;
    memset(normalized.clear_color, 0, sizeof(normalized.clear_color));
    *out = normalized;
    return DOE_RESOURCE_OK;
}

/* Targets have already passed attachment admission, so at least one exists. */
doe_resource_render_bundle_desc resource_render_targets_layout(const doe_resource_render_targets *targets)
{
    doe_resource_render_bundle_desc layout = {
        .depth_stencil_format = targets->depth_stencil ? targets->depth_stencil->desc.format : WGPUTextureFormat_Undefined,
        .depth_read_only = targets->depth_read_only, .stencil_read_only = targets->stencil_read_only,
        .color_count = targets->color_count};
    const doe_resource_view *primary = targets->depth_stencil;
    for (uint32_t i = 0; i < targets->color_count; i++) {
        const doe_resource_view *view = targets->colors[i].view;
        layout.color_formats[i] = view ? view->desc.format : WGPUTextureFormat_Undefined;
        if (view && !primary) primary = view;
    }
    layout.samples = primary->texture->desc.samples;
    return layout;
}

bool resource_color_layouts_equal(uint32_t a_count, const WGPUTextureFormat *a,
    uint32_t b_count, const WGPUTextureFormat *b)
{
    uint32_t count = a_count > b_count ? a_count : b_count;
    for (uint32_t i = 0; i < count; i++) {
        WGPUTextureFormat left = i < a_count ? a[i] : WGPUTextureFormat_Undefined;
        WGPUTextureFormat right = i < b_count ? b[i] : WGPUTextureFormat_Undefined;
        if (left != right) return false;
    }
    return true;
}
