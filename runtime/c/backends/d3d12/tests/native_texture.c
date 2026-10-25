/* SPDX-License-Identifier: MIT */
#include "native_fixture.h"
_Static_assert(sizeof(float) == 4, "The R32_FLOAT fixture requires four-byte floats.");

/* Real SDK/driver fixture only: compile native_texture.hlsl separately with DXC.
 * Deliberately padded rows, static sampler, sampled/storage views, texture copy,
 * early caller release and exact readback. Building this does not execute it. */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: native_texture.exe native_texture.dxil\n");
        return 2;
    }
    size_t length = 0;
    unsigned char *bytecode = read_bytecode(argv[1], &length);
    if (!bytecode) return 2;
    doe_dx_context *context = NULL;
    doe_dx_buffer *upload = NULL, *readback = NULL;
    doe_dx_texture *input = NULL, *output = NULL, *copy = NULL;
    doe_dx_pipeline *pipeline = NULL;
    doe_dx_commands *commands = NULL;
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
    doe_dx_texture_desc desc = {3, 2, DOE_DX_R32_FLOAT, DOE_DX_TEXTURE_COPY_DST | DOE_DX_TEXTURE_SAMPLED};
    status = doe_dx_texture_create(context, &desc, &input, &error);
    if (status != DOE_DX_OK) goto failed;
    desc.usage = DOE_DX_TEXTURE_COPY_DST | DOE_DX_TEXTURE_COPY_SRC | DOE_DX_TEXTURE_STORAGE;
    status = doe_dx_texture_create(context, &desc, &output, &error);
    if (status != DOE_DX_OK) goto failed;
    desc.usage = DOE_DX_TEXTURE_COPY_DST | DOE_DX_TEXTURE_COPY_SRC;
    status = doe_dx_texture_create(context, &desc, &copy, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_texture_layout layout = {0};
    doe_dx_texture_footprint footprint;
    status = doe_dx_texture_copy_footprint(input, layout, &footprint);
    if (status != DOE_DX_OK) goto failed;
    if (footprint.row_pitch != 256 || footprint.row_bytes != 12 || footprint.span != 268) goto cleanup;
    float values[6] = {1, 2, 4, 8, 16, 32};
    unsigned char expected[268] = {0}, actual[268];
    for (size_t row = 0; row < 2; row++)
        memcpy(expected + row * 256, values + row * 3, 3 * sizeof(float));
    status = doe_dx_buffer_create(context, sizeof(expected), DOE_DX_UPLOAD, &upload, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(expected), DOE_DX_READBACK, &readback, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_write(upload, 0, expected, sizeof(expected), &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_texture_binding_layout textures[2] = {
        {0, 0, DOE_DX_R32_FLOAT, DOE_DX_READ_ONLY, false},
        {0, 0, DOE_DX_R32_FLOAT, DOE_DX_READ_WRITE, false}
    };
    doe_dx_static_sampler sampler = {0, 0, DOE_DX_NEAREST, DOE_DX_CLAMP, DOE_DX_CLAMP};
    doe_dx_resource_pipeline_desc pipeline_desc = {{bytecode, length, NULL, 0}, textures, 2, &sampler, 1};
    status = doe_dx_pipeline_create_resources(context, &pipeline_desc, &pipeline, &error);
    if (status != DOE_DX_OK) goto failed;
    free(bytecode);
    bytecode = NULL;
    status = doe_dx_commands_create(context, &commands, &error);
    if (status != DOE_DX_OK) goto failed;
    /* Native texture copies do not write padding. Initialize the whole readback. */
    status = doe_dx_commands_copy(commands, upload, 0, readback, 0, sizeof(expected), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_upload_texture(commands, upload, layout, input, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_upload_texture(commands, upload, layout, output, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_texture *bindings[2] = {input, output};
    doe_dx_compute_resources resources = {NULL, 0, bindings, 2};
    status = doe_dx_commands_dispatch_resources(commands, pipeline, &resources, 1, 1, 1, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy_texture(commands, output, copy, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_read_texture(commands, copy, readback, layout, &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_texture_release(input);
    input = NULL;
    doe_dx_texture_release(output);
    output = NULL;
    doe_dx_texture_release(copy);
    copy = NULL;
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
    status = doe_dx_buffer_read(readback, 0, actual, sizeof(actual), &error);
    if (status != DOE_DX_OK) goto failed;
    for (size_t index = 0; index < 6; index++) values[index] += 0.25f;
    for (size_t row = 0; row < 2; row++)
        memcpy(expected + row * 256, values + row * 3, 3 * sizeof(float));
    for (size_t index = 0; index < sizeof(expected); index++) {
        if (expected[index] != actual[index]) {
            fprintf(stderr, "texture byte mismatch[%zu]: expected=%u actual=%u\n",
                index, (unsigned)expected[index], (unsigned)actual[index]);
            goto cleanup;
        }
    }
    puts("native D3D12 sampled/storage texture readback and early caller release passed");
    result = 0;
    goto cleanup;
failed:
    report("native texture", status, &error);
cleanup:
    free(bytecode);
    doe_dx_commands_release(commands);
    doe_dx_pipeline_release(pipeline);
    doe_dx_texture_release(input);
    doe_dx_texture_release(output);
    doe_dx_texture_release(copy);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(readback);
    if (context) {
        doe_dx_status closed = doe_dx_context_destroy(context);
        if (closed != DOE_DX_OK) {
            fprintf(stderr, "unresolved native work retained; context destruction refused (%d)\n", (int)closed);
            result = 1;
        }
    }
    return result;
}
