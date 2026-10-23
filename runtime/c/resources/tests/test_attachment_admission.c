/* SPDX-License-Identifier: MIT */
#include "fixture.h"
#include <math.h>

static void check_texture(const doe_resource_texture_desc *desc, doe_resource_status expected)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_texture_limits limits = texture_limits();
    native_token token = {0};
    doe_resource_texture *texture = NULL;
    assert(doe_resource_texture_validate(desc, &limits) == expected);
    assert(doe_resource_texture_create(&c, desc, &limits, native(&f, &token), &texture) == expected);
    if (expected == DOE_RESOURCE_OK) {
        assert(texture && !token.releases);
        doe_resource_texture_release(texture);
        assert(token.releases == 1);
    } else {
        assert(!texture && !token.releases && !f.allocations);
    }
    empty(&f);
}

static void test_texture_descriptor(void)
{
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_RenderAttachment;
    check_texture(&desc, DOE_RESOURCE_OK);
    desc.samples = 4;
    check_texture(&desc, DOE_RESOURCE_OK);
    desc.samples = 1;
    const uint64_t forbidden[] = {WGPUTextureUsage_CopySrc, WGPUTextureUsage_CopyDst,
        WGPUTextureUsage_TextureBinding, WGPUTextureUsage_StorageBinding};
    for (size_t i = 0; i < sizeof(forbidden) / sizeof(*forbidden); i++) {
        desc.usage |= forbidden[i];
        check_texture(&desc, DOE_RESOURCE_USAGE);
        desc.usage &= ~forbidden[i];
    }
    desc.usage = WGPUTextureUsage_TransientAttachment;
    check_texture(&desc, DOE_RESOURCE_USAGE);
    desc.usage |= WGPUTextureUsage_RenderAttachment;
    WGPUTextureFormat same = desc.format;
    desc.view_formats = &same;
    desc.view_format_count = 1;
    check_texture(&desc, DOE_RESOURCE_LAYOUT);
    desc.view_formats = NULL;
    desc.view_format_count = 0;
    desc.mip_levels = 2;
    check_texture(&desc, DOE_RESOURCE_LAYOUT);
    desc.mip_levels = 1;
    desc.depth_or_layers = 2;
    check_texture(&desc, DOE_RESOURCE_LAYOUT);
    desc.depth_or_layers = 1;
    desc.dimension = WGPUTextureDimension_3D;
    check_texture(&desc, DOE_RESOURCE_LAYOUT);
    desc.dimension = WGPUTextureDimension_1D;
    desc.height = 1;
    check_texture(&desc, DOE_RESOURCE_LAYOUT);
    desc.usage = WGPUTextureUsage_TextureBinding;
    check_texture(&desc, DOE_RESOURCE_OK);
    desc.mip_levels = 2;
    check_texture(&desc, DOE_RESOURCE_RANGE);
    desc.mip_levels = 1;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    check_texture(&desc, DOE_RESOURCE_USAGE);
    desc = texture_desc();
    desc.samples = 4;
    desc.usage = WGPUTextureUsage_TextureBinding;
    check_texture(&desc, DOE_RESOURCE_USAGE);
    desc.usage |= WGPUTextureUsage_RenderAttachment;
    check_texture(&desc, DOE_RESOURCE_OK);
}

static doe_resource_view *attachment(fixture *f, native_token tokens[2],
    WGPUTextureFormat format, bool transient)
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    desc.format = format;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    if (transient) desc.usage |= WGPUTextureUsage_TransientAttachment;
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0}, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}

static void test_transient_views(void)
{
    fixture f = fresh();
    native_token tokens[2] = {{0}};
    doe_resource_view *view = attachment(&f, tokens, WGPUTextureFormat_RGBA8Unorm, true);
    doe_resource_texture *texture = doe_resource_view_texture(view);
    const uint64_t usage = WGPUTextureUsage_TransientAttachment | WGPUTextureUsage_RenderAttachment;
    assert(doe_resource_view_description(view)->usage == usage);
    doe_resource_view_desc desc = {.usage = WGPUTextureUsage_RenderAttachment};
    doe_resource_view_desc output = {.base_mip = 123};
    doe_resource_view *failed = NULL;
    native_token token = {0};
    size_t allocations = f.allocations;
    assert(doe_resource_view_validate(texture, &desc, &output) == DOE_RESOURCE_USAGE);
    assert(output.base_mip == 123);
    assert(doe_resource_view_create(texture, &desc, native(&f, &token), &failed) == DOE_RESOURCE_USAGE);
    assert(!failed && !token.releases && f.allocations == allocations);
    desc.usage = WGPUTextureUsage_TransientAttachment;
    assert(doe_resource_view_validate(texture, &desc, &output) == DOE_RESOURCE_USAGE);
    desc.usage = usage;
    assert(doe_resource_view_validate(texture, &desc, &output) == DOE_RESOURCE_OK && output.usage == usage);
    doe_resource_view_release(view);
    assert(tokens[0].releases == 1 && tokens[1].releases == 1);
    empty(&f);
}

/* The same targets are admitted at begin and rechecked from immutable command
 * records. Tokens are ownership fixtures, never simulated attachment contents. */
static void check_pass(WGPUTextureFormat format, bool color_transient, bool depth_transient,
    doe_resource_render_targets targets, doe_resource_status expected)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token tokens[4] = {{0}};
    targets.color = attachment(&f, tokens, WGPUTextureFormat_RGBA8Unorm, color_transient);
    if (format) targets.depth_stencil = attachment(&f, &tokens[2], format, depth_transient);
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass = NULL;
    assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
    size_t allocations = f.allocations;
    unsigned references = f.device_references;
    assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == expected);
    doe_resource_commands *commands = NULL;
    if (expected == DOE_RESOURCE_OK) {
        assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
        doe_resource_render_pass_release(pass);
        assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
        size_t count;
        const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
        assert(count == 2);
        const doe_resource_render_packet *first = doe_resource_render_record_borrow(records[0].value.render);
        assert(first->first_in_pass && first->targets.color == targets.color);
        assert(first->targets.depth_stencil == targets.depth_stencil);
    } else {
        assert(!pass && f.allocations == allocations && f.device_references == references);
        assert(doe_resource_encoder_finish(encoder, &commands) == expected && !commands);
    }
    doe_resource_encoder_release(encoder);
    doe_resource_view_release(targets.color);
    doe_resource_view_release(targets.depth_stencil);
    if (commands) {
        for (size_t i = 0; i < (format ? 4u : 2u); i++) assert(!tokens[i].releases);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
        doe_resource_commands_release(commands);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    }
    for (size_t i = 0; i < (format ? 4u : 2u); i++) assert(tokens[i].releases == 1);
    empty(&f);
}

static void test_attachment_aspects(void)
{
    const struct { WGPUTextureFormat format; bool depth, stencil; } formats[] = {
        {WGPUTextureFormat_Depth16Unorm, true, false},
        {WGPUTextureFormat_Depth24Plus, true, false},
        {WGPUTextureFormat_Depth32Float, true, false},
        {WGPUTextureFormat_Stencil8, false, true},
        {WGPUTextureFormat_Depth24PlusStencil8, true, true},
        {WGPUTextureFormat_Depth32FloatStencil8, true, true}
    };
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
        doe_resource_render_targets t = {.color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
        if (formats[i].depth) { t.depth_load = WGPULoadOp_Clear; t.depth_store = WGPUStoreOp_Store; }
        if (formats[i].stencil) { t.stencil_load = WGPULoadOp_Clear; t.stencil_store = WGPUStoreOp_Store; }
        check_pass(formats[i].format, false, false, t, DOE_RESOURCE_OK);
        doe_resource_render_targets changed = t;
        changed.depth_load = WGPULoadOp_Undefined;
        changed.depth_store = WGPUStoreOp_Undefined;
        changed.stencil_load = WGPULoadOp_Undefined;
        changed.stencil_store = WGPUStoreOp_Undefined;
        changed.depth_read_only = true;
        changed.stencil_read_only = true;
        changed.clear_depth = NAN;
        check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_OK);
        check_pass(formats[i].format, false, true, changed, DOE_RESOURCE_LAYOUT);
        if (formats[i].depth) {
            changed = t;
            changed.depth_read_only = true;
            changed.depth_load = WGPULoadOp_Undefined;
            changed.depth_store = WGPUStoreOp_Undefined;
            changed.clear_depth = NAN;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_OK);
        }
        if (formats[i].stencil) {
            changed = t;
            changed.stencil_read_only = true;
            changed.stencil_load = WGPULoadOp_Undefined;
            changed.stencil_store = WGPUStoreOp_Undefined;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_OK);
        }
        if (!formats[i].depth) {
            changed = t;
            changed.depth_load = WGPULoadOp_Clear;
            changed.depth_store = WGPUStoreOp_Store;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_LAYOUT);
        }
        if (!formats[i].stencil) {
            changed = t;
            changed.stencil_load = WGPULoadOp_Clear;
            changed.stencil_store = WGPUStoreOp_Store;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_LAYOUT);
        }
        if (formats[i].depth) {
            changed = t;
            changed.depth_read_only = true;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_LAYOUT);
            changed = t;
            changed.clear_depth = NAN;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_RANGE);
            changed.depth_load = WGPULoadOp_Load;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_OK);
            t.depth_store = WGPUStoreOp_Discard;
        }
        if (formats[i].stencil) {
            changed = t;
            changed.stencil_read_only = true;
            check_pass(formats[i].format, false, false, changed, DOE_RESOURCE_LAYOUT);
            t.stencil_store = WGPUStoreOp_Discard;
        }
        check_pass(formats[i].format, false, true, t, DOE_RESOURCE_OK);
        changed = t;
        if (formats[i].depth) changed.depth_load = WGPULoadOp_Load;
        else changed.stencil_load = WGPULoadOp_Load;
        check_pass(formats[i].format, false, true, changed, DOE_RESOURCE_LAYOUT);
        changed = t;
        if (formats[i].stencil) changed.stencil_store = WGPUStoreOp_Store;
        else changed.depth_store = WGPUStoreOp_Store;
        check_pass(formats[i].format, false, true, changed, DOE_RESOURCE_LAYOUT);
    }
}

static void test_transient_color(void)
{
    doe_resource_render_targets t = {.color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Discard};
    check_pass(WGPUTextureFormat_Undefined, true, false, t, DOE_RESOURCE_OK);
    t.color_store = WGPUStoreOp_Store;
    check_pass(WGPUTextureFormat_Undefined, true, false, t, DOE_RESOURCE_LAYOUT);
    t.color_store = WGPUStoreOp_Discard;
    t.color_load = WGPULoadOp_Load;
    check_pass(WGPUTextureFormat_Undefined, true, false, t, DOE_RESOURCE_LAYOUT);
    t.color_load = WGPULoadOp_Clear;
    t.clear_color[2] = NAN;
    check_pass(WGPUTextureFormat_Undefined, true, false, t, DOE_RESOURCE_RANGE);
}

int main(void)
{
    test_texture_descriptor();
    test_transient_views();
    test_attachment_aspects();
    test_transient_color();
    puts("attachment admission owner: 4 host suites passed; no attachment GPU execution claimed");
    return 0;
}
