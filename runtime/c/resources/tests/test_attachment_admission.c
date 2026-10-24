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
        assert(first->first_in_pass && first->targets.color_count == 1 && first->targets.colors[0].view == targets.color && !first->targets.color);
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

static doe_resource_status begin_slices(doe_resource_config *c, doe_resource_color_attachment a,
    doe_resource_color_attachment b, uint32_t count)
{
    doe_resource_render_targets targets = {.color_count = count, .colors = {a, b}};
    doe_resource_encoder *encoder;
    doe_resource_render_pass *pass = NULL;
    assert(doe_resource_encoder_create(c, &encoder) == DOE_RESOURCE_OK);
    doe_resource_status status = doe_resource_encoder_begin_render(encoder, &targets, &pass);
    if (status == DOE_RESOURCE_OK) {
        assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
        doe_resource_render_pass_release(pass);
        doe_resource_commands *commands = NULL;
        assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
        size_t records;
        const doe_resource_command *list = doe_resource_commands_borrow(commands, &records);
        const doe_resource_render_packet *first = doe_resource_render_record_borrow(list[0].value.render);
        assert(first->targets.colors[0].has_depth_slice == a.has_depth_slice &&
            first->targets.colors[0].depth_slice == a.depth_slice);
        assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
        doe_resource_commands_release(commands);
        assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    }
    doe_resource_encoder_release(encoder);
    return status;
}

/* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpurenderpasscolorattachment:
 * a 3D view renders one depth slice below its mip's depth, a 2D view none, and
 * two slots may not render the same (texture, mip, slice). */
static void test_depth_slices(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture_desc desc = texture_desc();
    desc.dimension = WGPUTextureDimension_3D;
    desc.depth_or_layers = 4;
    desc.mip_levels = 2;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    native_token tokens[4] = {{0}};
    doe_resource_texture *volume, *flat;
    doe_resource_view *mip0, *mip1, *plane;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &tokens[0]), &volume) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(volume, &(doe_resource_view_desc){.mip_count = 1}, native(&f, &tokens[1]), &mip0) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(volume, &(doe_resource_view_desc){.base_mip = 1, .mip_count = 1}, native(&f, &tokens[2]), &mip1) == DOE_RESOURCE_OK);
    assert(doe_resource_view_description(mip0)->dimension == WGPUTextureViewDimension_3D);
    doe_resource_texture_desc flat_desc = texture_desc();
    flat_desc.usage = WGPUTextureUsage_RenderAttachment;
    assert(doe_resource_texture_create(&c, &flat_desc, &limits, native(&f, &tokens[3]), &flat) == DOE_RESOURCE_OK);
    native_token plane_token = {0};
    assert(doe_resource_view_create(flat, &(doe_resource_view_desc){0}, native(&f, &plane_token), &plane) == DOE_RESOURCE_OK);
    const doe_resource_color_attachment none = {0};
    doe_resource_color_attachment slice = {.view = mip0, .load = WGPULoadOp_Clear, .store = WGPUStoreOp_Store,
        .has_depth_slice = true, .depth_slice = 3};
    assert(begin_slices(&c, slice, none, 1) == DOE_RESOURCE_OK);
    slice.depth_slice = 4;
    assert(begin_slices(&c, slice, none, 1) == DOE_RESOURCE_RANGE);
    slice.has_depth_slice = false;
    slice.depth_slice = 0;
    assert(begin_slices(&c, slice, none, 1) == DOE_RESOURCE_RANGE);
    /* Mip 1 of a depth-4 volume has two slices. */
    slice = (doe_resource_color_attachment){.view = mip1, .load = WGPULoadOp_Clear, .store = WGPUStoreOp_Store,
        .has_depth_slice = true, .depth_slice = 1};
    assert(begin_slices(&c, slice, none, 1) == DOE_RESOURCE_OK);
    slice.depth_slice = 2;
    assert(begin_slices(&c, slice, none, 1) == DOE_RESOURCE_RANGE);
    /* Two slots: distinct slices of one mip render together; one slice twice aliases. */
    doe_resource_color_attachment first = {.view = mip0, .load = WGPULoadOp_Clear, .store = WGPUStoreOp_Store,
        .has_depth_slice = true, .depth_slice = 0};
    doe_resource_color_attachment second = first;
    second.depth_slice = 2;
    assert(begin_slices(&c, first, second, 2) == DOE_RESOURCE_OK);
    second.depth_slice = 0;
    assert(begin_slices(&c, first, second, 2) == DOE_RESOURCE_ALIAS);
    /* A 2D view never names a depth slice. */
    doe_resource_color_attachment planar = {.view = plane, .load = WGPULoadOp_Clear, .store = WGPUStoreOp_Store,
        .has_depth_slice = true};
    assert(begin_slices(&c, planar, none, 1) == DOE_RESOURCE_LAYOUT);
    planar.has_depth_slice = false;
    assert(begin_slices(&c, planar, none, 1) == DOE_RESOURCE_OK);
    doe_resource_view_release(mip0);
    doe_resource_view_release(mip1);
    doe_resource_view_release(plane);
    doe_resource_texture_release(volume);
    doe_resource_texture_release(flat);
    empty(&f);
}

/* A swizzled view is created with RenderAttachment usage; WebGPU rejects it as
 * a color attachment, resolve target or depth-stencil attachment. */
static void test_swizzled_views(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture_desc desc = texture_desc();
    desc.usage = WGPUTextureUsage_RenderAttachment;
    native_token tokens[3] = {{0}};
    doe_resource_texture *texture;
    doe_resource_view *plain, *swizzled;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0}, native(&f, &tokens[1]), &plain) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){.swizzled = true},
        native(&f, &tokens[2]), &swizzled) == DOE_RESOURCE_OK);
    assert(doe_resource_view_description(swizzled)->swizzled && !doe_resource_view_description(plain)->swizzled);
    doe_resource_view *views[2] = {plain, swizzled};
    for (size_t i = 0; i < 2; i++) {
        doe_resource_encoder *encoder;
        doe_resource_render_pass *pass = NULL;
        doe_resource_commands *commands = NULL;
        doe_resource_render_targets targets = {.color = views[i],
            .color_load = WGPULoadOp_Clear, .color_store = WGPUStoreOp_Store};
        doe_resource_status expected = i ? DOE_RESOURCE_LAYOUT : DOE_RESOURCE_OK;
        assert(doe_resource_encoder_create(&c, &encoder) == DOE_RESOURCE_OK);
        assert(doe_resource_encoder_begin_render(encoder, &targets, &pass) == expected);
        if (pass) {
            assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
            doe_resource_render_pass_release(pass);
        }
        assert(doe_resource_encoder_finish(encoder, &commands) == expected);
        doe_resource_commands_release(commands);
        doe_resource_encoder_release(encoder);
    }
    doe_resource_view_release(plain);
    doe_resource_view_release(swizzled);
    doe_resource_texture_release(texture);
    for (size_t i = 0; i < 3; i++) assert(tokens[i].releases == 1);
    empty(&f);
}

int main(void)
{
    test_texture_descriptor();
    test_transient_views();
    test_attachment_aspects();
    test_transient_color();
    test_depth_slices();
    test_swizzled_views();
    puts("attachment admission owner: 6 host suites passed; no attachment GPU execution claimed");
    return 0;
}
