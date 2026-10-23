/* SPDX-License-Identifier: MIT
 * Indexed color slots: sparse layouts, legacy slot-0 normalization, pass/
 * pipeline/bundle layout equality ignoring trailing unused slots, slot count and
 * color-bytes-per-sample limits, subresource aliasing and per-slot operations.
 * Host admission only; native renderability and execution are separate. */
#include "fixture.h"

static doe_resource_view *attachment(fixture *f, native_token tokens[2], WGPUTextureFormat format,
    uint32_t width)
{
    doe_resource_config c = config(f);
    doe_resource_texture_desc desc = texture_desc();
    doe_resource_texture_limits limits = texture_limits();
    desc.format = format;
    desc.width = width;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    doe_resource_texture *texture;
    doe_resource_view *view;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(f, &tokens[0]), &texture) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(texture, &(doe_resource_view_desc){0}, native(f, &tokens[1]), &view) == DOE_RESOURCE_OK);
    doe_resource_texture_release(texture);
    return view;
}

static doe_resource_color_attachment slot(doe_resource_view *view)
{
    return (doe_resource_color_attachment){.view = view, .load = WGPULoadOp_Clear, .store = WGPUStoreOp_Store};
}

static doe_resource_status begin(doe_resource_config *c, const doe_resource_render_targets *targets,
    doe_resource_render_pass **pass, doe_resource_encoder **encoder)
{
    assert(doe_resource_encoder_create(c, encoder) == DOE_RESOURCE_OK);
    return doe_resource_encoder_begin_render(*encoder, targets, pass);
}

static void expect_pass(doe_resource_config c, const doe_resource_render_targets *targets, doe_resource_status expected)
{
    doe_resource_render_pass *pass = NULL;
    doe_resource_encoder *encoder;
    assert(begin(&c, targets, &pass, &encoder) == expected);
    if (pass) {
        assert(doe_resource_render_pass_end(pass) == DOE_RESOURCE_OK);
        doe_resource_render_pass_release(pass);
    }
    doe_resource_encoder_release(encoder);
}

static doe_resource_status pipeline(fixture *f, native_token *token, const WGPUTextureFormat *formats,
    uint32_t count, doe_resource_config c, doe_resource_render_pipeline **out)
{
    doe_resource_pipeline_layout *layout;
    assert(doe_resource_pipeline_layout_create(&c, NULL, 0, 0, &layout) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_desc desc = {.samples = 1, .color_count = count};
    for (uint32_t i = 0; i < count; i++) desc.color_formats[i] = formats[i];
    doe_resource_status status = doe_resource_render_pipeline_create(&c, layout, &desc, native(f, token), out);
    doe_resource_pipeline_layout_release(layout);
    return status;
}

static void test_sparse_layout(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token tokens[8] = {{0}};
    doe_resource_view *a = attachment(&f, &tokens[0], WGPUTextureFormat_RGBA8Unorm, 4);
    doe_resource_view *b = attachment(&f, &tokens[2], WGPUTextureFormat_R32Uint, 4);
    doe_resource_render_targets targets = {.color_count = 3, .colors = {slot(a), {0}, slot(b)}};
    doe_resource_render_pass *pass = NULL;
    doe_resource_encoder *encoder;
    assert(begin(&c, &targets, &pass, &encoder) == DOE_RESOURCE_OK);
    native_token pipeline_tokens[4] = {{0}};
    const WGPUTextureFormat same[4] = {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Undefined,
        WGPUTextureFormat_R32Uint, WGPUTextureFormat_Undefined};
    const WGPUTextureFormat moved[2] = {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_R32Uint};
    doe_resource_render_pipeline *matching = NULL, *trailing = NULL, *wrong = NULL;
    assert(pipeline(&f, &pipeline_tokens[0], same, 3, c, &matching) == DOE_RESOURCE_OK);
    assert(pipeline(&f, &pipeline_tokens[1], same, 4, c, &trailing) == DOE_RESOURCE_OK);
    assert(pipeline(&f, &pipeline_tokens[2], moved, 2, c, &wrong) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_pipeline(pass, matching) == DOE_RESOURCE_OK);
    assert(doe_resource_render_pass_set_pipeline(pass, trailing) == DOE_RESOURCE_OK);
    /* A sparse slot is not interchangeable with packed indices. */
    assert(doe_resource_render_pass_set_pipeline(pass, wrong) == DOE_RESOURCE_LAYOUT);
    doe_resource_render_pass_release(pass);
    doe_resource_encoder_release(encoder);
    /* Bundles carry the same sparse signature. */
    doe_resource_render_bundle_desc bundle = {.samples = 1, .color_count = 3,
        .color_formats = {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Undefined, WGPUTextureFormat_R32Uint}};
    doe_resource_bundle_encoder *bundle_encoder = NULL;
    assert(doe_resource_bundle_encoder_create(&c, &bundle, &bundle_encoder) == DOE_RESOURCE_OK);
    const doe_resource_render_bundle_desc *stored = doe_resource_bundle_encoder_description(bundle_encoder);
    assert(stored->color_count == 3 && stored->color_formats[2] == WGPUTextureFormat_R32Uint && !stored->color_format);
    doe_resource_bundle_encoder_release(bundle_encoder);
    bundle.color_format = WGPUTextureFormat_RGBA8Unorm; /* Legacy slot 0 plus slots: rejected. */
    assert(doe_resource_bundle_encoder_create(&c, &bundle, &bundle_encoder) == DOE_RESOURCE_LAYOUT);
    bundle = (doe_resource_render_bundle_desc){.samples = 1, .color_count = 2};
    assert(doe_resource_bundle_encoder_create(&c, &bundle, &bundle_encoder) == DOE_RESOURCE_LAYOUT); /* All unused. */
    doe_resource_render_pipeline_release(matching);
    doe_resource_render_pipeline_release(trailing);
    doe_resource_render_pipeline_release(wrong);
    doe_resource_view_release(a);
    doe_resource_view_release(b);
    empty(&f);
}

static void test_limits_and_aliases(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    native_token tokens[8] = {{0}};
    doe_resource_view *wide = attachment(&f, &tokens[0], WGPUTextureFormat_RGBA32Float, 4);
    doe_resource_view *narrow = attachment(&f, &tokens[2], WGPUTextureFormat_R8Unorm, 4);
    doe_resource_view *word = attachment(&f, &tokens[4], WGPUTextureFormat_R32Float, 4);
    doe_resource_view *smaller = attachment(&f, &tokens[6], WGPUTextureFormat_R8Unorm, 2);
    /* 16 + 16 = 32 bytes fits the baseline; a third 16-byte slot does not. */
    doe_resource_render_targets targets = {.color_count = 2, .colors = {slot(wide), {0}}};
    expect_pass(c, &targets, DOE_RESOURCE_OK);
    native_token pipeline_tokens[3] = {{0}};
    doe_resource_render_pipeline *created = NULL;
    const WGPUTextureFormat heavy[3] = {WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_RGBA32Float,
        WGPUTextureFormat_RGBA32Float};
    assert(pipeline(&f, &pipeline_tokens[0], heavy, 2, c, &created) == DOE_RESOURCE_OK);
    doe_resource_render_pipeline_release(created);
    created = NULL;
    assert(pipeline(&f, &pipeline_tokens[1], heavy, 3, c, &created) == DOE_RESOURCE_RANGE && !created);
    /* Alignment: R8 (1) then R32Float aligned to 4 is 8 bytes, not 5. */
    targets = (doe_resource_render_targets){.color_count = 2, .colors = {slot(narrow), slot(word)}};
    doe_resource_config tight = c;
    tight.max_color_attachment_bytes_per_sample = 7;
    expect_pass(tight, &targets, DOE_RESOURCE_RANGE);
    tight.max_color_attachment_bytes_per_sample = 8;
    expect_pass(tight, &targets, DOE_RESOURCE_OK);
    /* Slot count above the requested limit, even when trailing slots are unused. */
    tight = c;
    tight.max_color_attachments = 1;
    expect_pass(tight, &targets, DOE_RESOURCE_RANGE);
    targets = (doe_resource_render_targets){.color_count = 9};
    expect_pass(c, &targets, DOE_RESOURCE_RANGE);
    /* Unused slots carry no operations; all used slots share one extent. */
    targets = (doe_resource_render_targets){.color_count = 2, .colors = {slot(narrow), {.load = WGPULoadOp_Clear}}};
    expect_pass(c, &targets, DOE_RESOURCE_LAYOUT);
    targets = (doe_resource_render_targets){.color_count = 2, .colors = {slot(narrow), slot(smaller)}};
    expect_pass(c, &targets, DOE_RESOURCE_LAYOUT);
    targets = (doe_resource_render_targets){.color_count = 2, .colors = {slot(narrow), slot(narrow)}};
    expect_pass(c, &targets, DOE_RESOURCE_ALIAS);
    /* Legacy slot-0 fields are an input convenience, never mixed with slots. */
    targets = (doe_resource_render_targets){.color = narrow, .color_load = WGPULoadOp_Clear,
        .color_store = WGPUStoreOp_Store, .color_count = 1, .colors = {slot(narrow)}};
    expect_pass(c, &targets, DOE_RESOURCE_LAYOUT);
    targets = (doe_resource_render_targets){.color_count = 3};
    expect_pass(c, &targets, DOE_RESOURCE_LAYOUT); /* No attachment at all. */
    doe_resource_view_release(wide);
    doe_resource_view_release(narrow);
    doe_resource_view_release(word);
    doe_resource_view_release(smaller);
    empty(&f);
}

int main(void)
{
    test_sparse_layout();
    test_limits_and_aliases();
    puts("color target owner: 2 host suites passed; no native attachment execution claimed");
    return 0;
}
