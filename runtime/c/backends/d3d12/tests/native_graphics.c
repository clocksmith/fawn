/* SPDX-License-Identifier: MIT */
#include "native_fixture.h"

/* Windows SDK/driver fixture only. The fullscreen procedural triangle samples
 * green input over a red clear. Exact RGBA bytes and actual timestamp ordering
 * are checked after caller references have been released. No host simulation. */
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: native_graphics.exe vertex.dxil fragment.dxil\n");
        return 2;
    }
    size_t vertex_size = 0, fragment_size = 0;
    unsigned char *vertex = read_bytecode(argv[1], &vertex_size);
    unsigned char *fragment = read_bytecode(argv[2], &fragment_size);
    if (!vertex || !fragment) {
        free(vertex);
        free(fragment);
        return 2;
    }
    doe_dx_context *context = NULL;
    doe_dx_commands *commands = NULL;
    doe_dx_pipeline *pipeline = NULL;
    doe_dx_texture *input = NULL, *color = NULL;
    doe_dx_buffer *upload = NULL, *pixels = NULL, *ticks = NULL;
    doe_dx_timestamps *timestamps = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_context_create(NULL, &context, &error);
    int result = 1;
    if (status != DOE_DX_OK) goto failed;
    doe_dx_caps caps;
    status = doe_dx_context_caps(context, &caps);
    if (status != DOE_DX_OK) goto failed;
    printf("adapter=%s vendor=%u device=%u LUID=%ld:%lu software=%d\n", caps.name,
        caps.vendor_id, caps.device_id, (long)caps.luid_high, (unsigned long)caps.luid_low,
        caps.software_adapter ? 1 : 0);
    uint64_t frequency = 0;
    status = doe_dx_timestamp_frequency(context, &frequency, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_timestamps_create(context, 2, &timestamps, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_texture_desc texture_desc = {64, 2, DOE_DX_RGBA8_UNORM, DOE_DX_TEXTURE_COPY_DST | DOE_DX_TEXTURE_SAMPLED};
    status = doe_dx_texture_create(context, &texture_desc, &input, &error);
    if (status != DOE_DX_OK) goto failed;
    texture_desc.usage = DOE_DX_TEXTURE_RENDER_ATTACHMENT | DOE_DX_TEXTURE_COPY_SRC;
    status = doe_dx_texture_create(context, &texture_desc, &color, &error);
    if (status != DOE_DX_OK) goto failed;
    unsigned char expected[512], actual[512];
    for (size_t index = 0; index < sizeof(expected); index += 4) {
        expected[index] = 0;
        expected[index + 1] = 255;
        expected[index + 2] = 0;
        expected[index + 3] = 255;
    }
    status = doe_dx_buffer_create(context, sizeof(expected), DOE_DX_UPLOAD, &upload, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_write(upload, 0, expected, sizeof(expected), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(expected), DOE_DX_READBACK, &pixels, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, 16, DOE_DX_READBACK, &ticks, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_texture_binding_layout layout = {0, 0, DOE_DX_RGBA8_UNORM, DOE_DX_READ_ONLY, false};
    doe_dx_static_sampler sampler = {0, 0, DOE_DX_NEAREST, DOE_DX_CLAMP, DOE_DX_CLAMP};
    doe_dx_graphics_pipeline_desc pipeline_desc = {
        vertex, vertex_size, {{fragment, fragment_size, NULL, 0}, &layout, 1, &sampler, 1}, DOE_DX_RGBA8_UNORM
    };
    status = doe_dx_pipeline_create_graphics(context, &pipeline_desc, &pipeline, &error);
    if (status != DOE_DX_OK) goto failed;
    free(vertex);
    vertex = NULL;
    free(fragment);
    fragment = NULL;
    status = doe_dx_commands_create(context, &commands, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_upload_texture(commands, upload, (doe_dx_texture_layout){0}, input, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_timestamp(commands, timestamps, 0, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_compute_resources resources = {NULL, 0, &input, 1};
    doe_dx_draw_desc draw = {color, true, {1, 0, 0, 1}, 3, 1, 0, 0};
    status = doe_dx_commands_draw(commands, pipeline, &resources, &draw, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_timestamp(commands, timestamps, 1, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_resolve_timestamps(commands, timestamps, 0, 2, ticks, 0, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_read_texture(commands, color, pixels, (doe_dx_texture_layout){0}, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_timestamps_release(timestamps);
    timestamps = NULL;
    doe_dx_texture_release(input);
    input = NULL;
    doe_dx_texture_release(color);
    color = NULL;
    doe_dx_buffer_release(upload);
    upload = NULL;
    doe_dx_pipeline_release(pipeline);
    pipeline = NULL;
    status = doe_dx_commands_submit(commands, &error);
    if (status != DOE_DX_OK && status != DOE_DX_COMPLETION_UNKNOWN) goto failed;
    doe_dx_commands_release(commands);
    commands = NULL;
    status = doe_dx_context_poll(context, 30000, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_read(pixels, 0, actual, sizeof(actual), &error);
    if (status != DOE_DX_OK) goto failed;
    if (memcmp(expected, actual, sizeof(actual))) {
        fprintf(stderr, "offscreen sampled draw produced unexpected RGBA bytes\n");
        goto cleanup;
    }
    uint64_t times[2];
    status = doe_dx_buffer_read(ticks, 0, times, sizeof(times), &error);
    if (status != DOE_DX_OK) goto failed;
    if (times[1] < times[0] || !frequency) {
        fprintf(stderr, "invalid timestamp ordering/frequency\n");
        goto cleanup;
    }
    printf("native sampled draw passed; timestamp ticks=%llu frequency=%llu\n",
        (unsigned long long)(times[1] - times[0]), (unsigned long long)frequency);
    result = 0;
    goto cleanup;
failed:
    report("native graphics/timestamp", status, &error);
cleanup:
    free(vertex);
    free(fragment);
    doe_dx_commands_release(commands);
    doe_dx_pipeline_release(pipeline);
    doe_dx_timestamps_release(timestamps);
    doe_dx_texture_release(input);
    doe_dx_texture_release(color);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(pixels);
    doe_dx_buffer_release(ticks);
    if (context) {
        doe_dx_status closed = doe_dx_context_destroy(context);
        if (closed != DOE_DX_OK) {
            fprintf(stderr, "unresolved native work retained; context destruction refused (%d)\n", (int)closed);
            result = 1;
        }
    }
    return result;
}
