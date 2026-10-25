/* SPDX-License-Identifier: MIT
 * Native call ownership controls; no drawable, pixels or display is simulated. */
#include "mock_native.h"
#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

typedef struct surface_test {
    mock_native mock;
    doe_mtl_native base;
    bool available;
    unsigned configurations, presentations;
} surface_test;

static bool supported(void *userdata, void *layer) { (void)userdata; return layer != NULL; }
static doe_mtl_status configure(void *userdata, void *device, void *layer,
    const doe_mtl_surface_desc *desc, void **out, doe_mtl_error *error) {
    surface_test *test = userdata;
    assert(layer && desc->width == 8 && desc->height == 4 && desc->format == DOE_MTL_BGRA8_UNORM);
    ++test->configurations;
    return test->base.buffer(userdata, device, 1, DOE_MTL_PRIVATE, out, error);
}
static doe_mtl_status acquire(void *userdata, void *layer, const doe_mtl_surface_desc *surface_desc,
    void **drawable, void **texture,
    doe_mtl_error *error) {
    surface_test *test = userdata;
    assert(surface_desc->width == 8 && surface_desc->height == 4);
    if (!test->available) return doe_mtl_fail(DOE_MTL_PENDING, "nextDrawable unavailable", error);
    doe_mtl_texture_desc desc = {.type=DOE_MTL_TEXTURE_2D,.format=DOE_MTL_BGRA8_UNORM,
        .width=8,.height=4,.depth_or_layers=1,.mip_levels=1,.sample_count=1,.usage=DOE_MTL_TEXTURE_RENDER_TARGET};
    assert(test->base.buffer(userdata, layer, 1, DOE_MTL_PRIVATE, drawable, error) == DOE_MTL_OK);
    return test->base.texture(userdata, layer, &desc, texture, error);
}
static doe_mtl_status present(void *userdata, void *command, void *drawable, doe_mtl_error *error) {
    surface_test *test = userdata;
    assert(command && drawable);
    ++test->presentations;
    return doe_mtl_fail(DOE_MTL_OK, "presentDrawable registered", error);
}
static size_t observe(void *userdata, void *command, void *drawable, bool wait, doe_mtl_error *error) {
    surface_test *test = userdata;
    assert(drawable);
    size_t status = test->base.observe(userdata, command, wait, error);
    return status;
}

int main(void) {
    surface_test test = {0};
    test.base = mock_driver(&test.mock);
    doe_mtl_native native = test.base;
    native.surface_supported = supported;
    native.surface_configure = configure;
    native.surface_acquire = acquire;
    native.surface_present = present;
    native.surface_observe = observe;
    doe_mtl_context *context = NULL;
    assert(doe_mtl_context_open(native, &context, NULL) == DOE_MTL_OK);
    doe_mtl_surface_desc desc = {8,4,DOE_MTL_TEXTURE_RENDER_TARGET,DOE_MTL_BGRA8_UNORM,true,true};
    doe_mtl_surface *surface = NULL;
    desc.format = DOE_MTL_RGBA8_UNORM;
    assert(doe_mtl_surface_create(context, &test, &desc, &surface, NULL) == DOE_MTL_UNSUPPORTED);
    assert(!surface && !test.configurations);
    desc.format = DOE_MTL_BGRA8_UNORM;
    assert(doe_mtl_surface_create(context, &test, &desc, &surface, NULL) == DOE_MTL_OK);
    doe_mtl_texture *texture = NULL;
    unsigned live = test.mock.live;
    assert(doe_mtl_surface_acquire(surface, &texture, NULL) == DOE_MTL_PENDING);
    assert(!texture && test.mock.live == live);
    test.available = true;
    assert(doe_mtl_surface_acquire(surface, &texture, NULL) == DOE_MTL_OK);
    doe_mtl_commands *commands = NULL;
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_present(commands, texture, NULL) == DOE_MTL_UNINITIALIZED);
    assert(!test.presentations && doe_mtl_commands_release(commands) == DOE_MTL_OK);
    texture->initialized[0] = DOE_MTL_ASPECT_COLOR; /* Existing initializer's independent completion. */
    assert(doe_mtl_commands_create(context, &commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_present(commands, texture, NULL) == DOE_MTL_OK);
    assert(test.presentations == 1 && texture->presentation_registered);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    mock_result(&test.mock, 3, 0); /* Unknown GPU work retains the drawable. */
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_PENDING);
    assert(doe_mtl_commands_wait(commands, NULL) == DOE_MTL_COMPLETION_UNKNOWN);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_BUSY && texture->drawable);
    doe_mtl_surface_release(surface);
    doe_mtl_texture_release(texture);
    doe_mtl_context_release(context);
    assert(!test.mock.closes && test.mock.live);
    /* A skipped-frame timestamp is deliberately unavailable in this native
     * seam: GPU completion releases our lease after registered presentation. */
    mock_result(&test.mock, 4, 0);
    assert(doe_mtl_commands_poll(commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    mock_assert_closed(&test.mock);
    puts("PASS Metal surface native-call ownership: validation, unavailable acquire rollback, initialized contents, skipped-frame retirement after GPU completion, unknown retention, early releases and final cleanup (host only)");
}
