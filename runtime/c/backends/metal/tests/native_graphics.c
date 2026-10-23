/* SPDX-License-Identifier: MIT
 * Explicit bounded offscreen Metal initializer/MSAA/empty-raster correctness.
 * Actual native MSL only, not public WGSL, presentation or qualification. */
#define _POSIX_C_SOURCE 200809L
#include "doe_metal_render.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WIDTH = 8, HEIGHT = 8, PITCH = 256, DEADLINE_SECONDS = 5 };
static const char source[] =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct Vertex { float4 position [[position]]; };\n"
    "vertex Vertex smoke_vertex(uint id [[vertex_id]], device atomic_uint *hits [[buffer(0)]]) {\n"
    "  atomic_fetch_add_explicit(&hits[id], 1u, memory_order_relaxed);\n"
    "  float2 positions[3] = {float2(-1,-1),float2(1,-1),float2(-1,1)};\n"
    "  Vertex v; v.position = float4(positions[id],0,1); return v;\n}\n"
    "fragment float4 smoke_red() { return float4(1,0,0,1); }\n"
    "fragment float4 smoke_green() { return float4(0,1,0,1); }\n";

static doe_mtl_status await_completion(doe_mtl_commands *commands, doe_mtl_error *error) {
    struct timespec start, now, pause = {0, 1000000};
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return DOE_MTL_COMPLETION_UNKNOWN;
    for (;;) {
        doe_mtl_status status = doe_mtl_commands_poll(commands, error);
        if (status != DOE_MTL_PENDING && status != DOE_MTL_COMPLETION_UNKNOWN) return status;
        if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec - start.tv_sec >= DEADLINE_SECONDS)
            return DOE_MTL_COMPLETION_UNKNOWN;
        (void)nanosleep(&pause, NULL);
    }
}

static doe_mtl_status texture_new(doe_mtl_context *context, bool multisampled,
    doe_mtl_texture **texture, doe_mtl_texture_view **view, doe_mtl_error *error) {
    doe_mtl_texture_desc desc = {.type = multisampled ? DOE_MTL_TEXTURE_2D_MS : DOE_MTL_TEXTURE_2D,
        .format = DOE_MTL_RGBA8_UNORM, .width = WIDTH, .height = HEIGHT, .depth_or_layers = 1,
        .mip_levels = 1, .usage = DOE_MTL_TEXTURE_RENDER_TARGET |
            (multisampled ? 0u : DOE_MTL_TEXTURE_COPY_SRC), .sample_count = multisampled ? 4u : 1u};
    doe_mtl_status status = doe_mtl_texture_create(context, &desc, texture, error);
    if (status != DOE_MTL_OK) return status;
    doe_mtl_texture_view_desc span = {.type = desc.type, .format = desc.format,
        .mip_count = 1, .layer_count = 1, .red = DOE_MTL_SWIZZLE_RED,
        .green = DOE_MTL_SWIZZLE_GREEN, .blue = DOE_MTL_SWIZZLE_BLUE, .alpha = DOE_MTL_SWIZZLE_ALPHA};
    return doe_mtl_texture_view_create(*texture, &span, view, error);
}

static doe_mtl_status pipeline_new(doe_mtl_context *context, const char *fragment,
    doe_mtl_render_pipeline **out, doe_mtl_error *error) {
    doe_mtl_render_pipeline_desc desc = {
        .vertex = {source, sizeof(source) - 1, "smoke_vertex", UINT32_MAX},
        .fragment = {source, sizeof(source) - 1, fragment, UINT32_MAX},
        .color_count = 1, .sample_count = 4, .alpha_to_coverage = true,
        .override_sample_mask = true, .sample_mask = 15};
    desc.colors[0] = (doe_mtl_color_target){.format = DOE_MTL_RGBA8_UNORM, .write_mask = 15};
    return doe_mtl_render_pipeline_create(context, &desc, out, error);
}

int main(int argc, char **argv) {
    doe_mtl_context *context = NULL;
    doe_mtl_texture *multisampled = NULL, *resolved = NULL;
    doe_mtl_texture_view *target = NULL, *resolve = NULL;
    doe_mtl_render_pipeline *red = NULL, *green = NULL;
    doe_mtl_buffer *counters = NULL, *output = NULL;
    doe_mtl_commands *commands = NULL;
    doe_mtl_error error = {0};
    doe_mtl_status status = DOE_MTL_OK;
    doe_mtl_caps caps = {0};
    uint32_t hits[4] = {0};
    unsigned char pixels[PITCH * HEIGHT];
    int result = 1;
    bool submitted = false;
    const char *permission = getenv("DOE_MTL_ALLOW_HARDWARE");
    if (argc != 2 || strcmp(argv[1], "--run") || !permission || strcmp(permission, "1")) {
        fputs("Explicit authorized run required: DOE_MTL_ALLOW_HARDWARE=1 native_graphics --run\n", stderr);
        return 2;
    }
    status = doe_mtl_context_create(&context, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_context_caps(context, &caps);
    if (status != DOE_MTL_OK) goto cleanup;
    printf("Metal graphics device: %s; 8x8 4x-MSAA zero-init, alpha coverage, two empty raster draws, resolve/readback\n", caps.device_name);
    status = texture_new(context, true, &multisampled, &target, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = texture_new(context, false, &resolved, &resolve, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = pipeline_new(context, "smoke_red", &red, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = pipeline_new(context, "smoke_green", &green, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_create(context, sizeof(hits), DOE_MTL_SHARED, &counters, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_create(context, sizeof(pixels), DOE_MTL_SHARED, &output, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_commands_create(context, &commands, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_commands_initialize_attachment(commands, target, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    doe_mtl_render_pass_desc pass = {.color_count = 1};
    pass.colors[0] = (doe_mtl_render_attachment){.view = target, .load = DOE_MTL_LOAD, .store = DOE_MTL_STORE};
    doe_mtl_buffer_binding binding = {0, counters, 0, sizeof(hits)};
    doe_mtl_render_draw draw = {.pipeline = red, .topology = 3, .count = 3, .instance_count = 1,
        .vertex = {.buffers = &binding, .buffer_count = 1}};
    status = doe_mtl_commands_render(commands, &pass, &draw, 1, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    /* A second initializer must preserve already initialized red pixels. */
    status = doe_mtl_commands_initialize_attachment(commands, target, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    draw.pipeline = green;
    draw.viewport_enabled = true;
    draw.viewport = (doe_mtl_viewport){WIDTH, 0, 0, HEIGHT, 0, 1};
    status = doe_mtl_commands_render(commands, &pass, &draw, 1, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    draw.viewport_enabled = false;
    draw.scissor_enabled = true;
    draw.scissor = (doe_mtl_scissor){0, HEIGHT, WIDTH, 0};
    pass.colors[0].store = DOE_MTL_RESOLVE;
    pass.colors[0].resolve_view = resolve;
    status = doe_mtl_commands_render(commands, &pass, &draw, 1, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_commands_read_texture(commands, resolved,
        (doe_mtl_texture_region){.width = WIDTH, .height = HEIGHT, .depth_or_layers = 1},
        output, (doe_mtl_texture_buffer_layout){.bytes_per_row = PITCH}, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    memset(pixels, 0xa5, sizeof(pixels));
    if (doe_mtl_buffer_read(output, 0, pixels, sizeof(pixels)) != DOE_MTL_BUSY || pixels[0] != 0xa5) {
        fputs("FAIL graphics output readable or modified before completion\n", stderr);
        goto cleanup;
    }
    /* The recorded owner, not caller references, retains unretained GPU inputs. */
    doe_mtl_render_pipeline_release(red); red = NULL;
    doe_mtl_render_pipeline_release(green); green = NULL;
    doe_mtl_texture_view_release(target); target = NULL;
    doe_mtl_texture_view_release(resolve); resolve = NULL;
    doe_mtl_texture_release(multisampled); multisampled = NULL;
    doe_mtl_texture_release(resolved); resolved = NULL;
    doe_mtl_context_release(context); context = NULL;
    status = doe_mtl_commands_submit(commands, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    submitted = true;
    status = await_completion(commands, &error);
    if (status == DOE_MTL_PENDING || status == DOE_MTL_COMPLETION_UNKNOWN) {
        fprintf(stderr, "UNKNOWN graphics completion; recorded leases retained until process exit\n");
        return 3;
    }
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_read(counters, 0, hits, sizeof(hits));
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_read(output, 0, pixels, sizeof(pixels));
    if (status != DOE_MTL_OK) goto cleanup;
    const unsigned char *inside = &pixels[6 * PITCH + 1 * 4];
    const unsigned char *outside = &pixels[1 * PITCH + 6 * 4];
    if (hits[0] != 3 || hits[1] != 3 || hits[2] != 3 || hits[3] != 0 ||
        memcmp(inside, (unsigned char[]){255, 0, 0, 255}, 4) ||
        memcmp(outside, (unsigned char[]){0, 0, 0, 0}, 4)) {
        fprintf(stderr, "FAIL graphics results: hits=%u/%u/%u/%u inside=%u,%u,%u,%u outside=%u,%u,%u,%u\n",
            hits[0], hits[1], hits[2], hits[3], inside[0], inside[1], inside[2], inside[3],
            outside[0], outside[1], outside[2], outside[3]);
        goto cleanup;
    }
    puts("PASS bounded native Metal graphics: zero-init/no-op preserve, 4x-MSAA alpha coverage/resolve, empty viewport/scissor still execute vertices, early leases, actual pixels");
    result = 0;
cleanup:
    if (status != DOE_MTL_OK)
        fprintf(stderr, "Metal graphics: %s operation=%s native=%" PRId64 " detail=%s\n",
            doe_mtl_status_name(status), error.operation ? error.operation : "none", error.native_code, error.message);
    if (commands && doe_mtl_commands_release(commands) == DOE_MTL_BUSY) {
        fprintf(stderr, "UNKNOWN %s graphics ownership; leases retained until process exit\n", submitted ? "submitted" : "recorded");
        return 3;
    }
    doe_mtl_render_pipeline_release(red);
    doe_mtl_render_pipeline_release(green);
    doe_mtl_texture_view_release(target);
    doe_mtl_texture_view_release(resolve);
    doe_mtl_texture_release(multisampled);
    doe_mtl_texture_release(resolved);
    doe_mtl_buffer_release(counters);
    doe_mtl_buffer_release(output);
    doe_mtl_context_release(context);
    return result;
}
