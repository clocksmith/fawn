/* SPDX-License-Identifier: MIT
 * Imported texture ownership controls, not IOSurface or GPU execution. */
#include "mock_native.h"
#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct import_test {
    mock_native mock;
    doe_mtl_native base;
    uint32_t plane_count;
    bool bad_shape;
    unsigned calls;
} import_test;

static doe_mtl_status import(void *userdata, void *device, doe_mtl_external_source source,
    void *handle, doe_mtl_native_external *out, doe_mtl_error *error)
{
    import_test *test = userdata;
    assert(handle == test && source == DOE_MTL_EXTERNAL_IOSURFACE);
    ++test->calls;
    doe_mtl_native_external result = {.count=test->plane_count};
    for (uint32_t i = 0; i < result.count; ++i) {
        result.desc[i] = (doe_mtl_texture_desc){.type=DOE_MTL_TEXTURE_2D,
            .format=i ? DOE_MTL_RG8_UNORM : DOE_MTL_R8_UNORM,
            .width=i ? 4 : 8,.height=i ? 2 : 4,.depth_or_layers=1,.mip_levels=1,
            .sample_count=1,.usage=DOE_MTL_TEXTURE_SAMPLED | DOE_MTL_TEXTURE_COPY_SRC};
        doe_mtl_status status = test->base.texture(userdata, device, &result.desc[i], &result.planes[i], error);
        if (status != DOE_MTL_OK) {
            for (uint32_t j = 0; j < i; ++j) test->base.release(userdata, result.planes[j]);
            return status;
        }
    }
    if (test->bad_shape) result.desc[result.count - 1].width = UINT32_MAX;
    *out = result;
    return DOE_MTL_OK;
}

int main(void)
{
    import_test test = {.plane_count=2};
    test.base = mock_driver(&test.mock);
    doe_mtl_native native = test.base;
    native.external_texture = import;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(native, &context, NULL) == DOE_MTL_OK);
    doe_mtl_external_planes output = {.count=77};
    unsigned live = test.mock.live;
    assert(doe_mtl_external_texture_import(context, (doe_mtl_external_source)9, &test, &output, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(output.count == 77 && !test.calls);
    test.bad_shape = true;
    assert(doe_mtl_external_texture_import(context, DOE_MTL_EXTERNAL_IOSURFACE, &test, &output, NULL) == DOE_MTL_INVALID_ARGUMENT);
    assert(output.count == 77 && test.mock.live == live && context->references == 1);
    test.bad_shape = false;
    test.mock.fail = MOCK_TEXTURE;
    assert(doe_mtl_external_texture_import(context, DOE_MTL_EXTERNAL_IOSURFACE, &test, &output, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(output.count == 77 && test.mock.live == live);
    test.mock.fail = MOCK_NONE;
    assert(doe_mtl_external_texture_import(context, DOE_MTL_EXTERNAL_IOSURFACE, &test, &output, NULL) == DOE_MTL_OK);
    assert(output.count == 2 && output.planes[0] && output.planes[1]);
    assert(output.planes[0]->initialized[0] == DOE_MTL_ASPECT_COLOR);
    assert(output.planes[1]->desc.format == DOE_MTL_RG8_UNORM);
    doe_mtl_texture_view_desc description = {.type=DOE_MTL_TEXTURE_2D,.format=DOE_MTL_R8_UNORM,
        .mip_count=1,.layer_count=1,.red=DOE_MTL_SWIZZLE_RED,.green=DOE_MTL_SWIZZLE_GREEN,
        .blue=DOE_MTL_SWIZZLE_BLUE,.alpha=DOE_MTL_SWIZZLE_ALPHA};
    doe_mtl_texture_view *view = NULL;
    assert(doe_mtl_texture_view_create(output.planes[0], &description, &view, NULL) == DOE_MTL_OK);
    doe_mtl_pipeline_desc pd = {.msl="kernel",.msl_length=6,.entrypoint="main",
        .workgroup={1,1,1},.sizes_slot=UINT32_MAX};
    doe_mtl_pipeline *pipeline = NULL;
    assert(doe_mtl_pipeline_create(context, &pd, &pipeline, NULL) == DOE_MTL_OK);
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    doe_mtl_texture_binding binding = {.slot=3,.view=view};
    assert(doe_mtl_commands_dispatch_resources(commands, pipeline, NULL, 0, &binding, 1, NULL, 0,
        (doe_mtl_dimensions){1,1,1}, 1, NULL) == DOE_MTL_OK);
    void *expected = doe_mtl_texture_view_native(view);
    assert(test.mock.resources.textures[3] == expected);
    doe_mtl_texture_view_release(view);
    doe_mtl_texture_release(output.planes[0]);
    doe_mtl_texture_release(output.planes[1]);
    doe_mtl_pipeline_release(pipeline);
    unsigned before = test.mock.releases_by_kind[MOCK_KIND_TEXTURE];
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(&test.mock, 3, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_PENDING);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY);
    assert(test.mock.releases_by_kind[MOCK_KIND_TEXTURE] == before);
    mock_result(&test.mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    assert(test.mock.releases_by_kind[MOCK_KIND_TEXTURE] == before + 1);
    doe_mtl_context_release(context);
    mock_assert_closed(&test.mock);
    puts("PASS imported Metal planes: rollback, existing view/command leases and unknown completion (host controls only)");
}
