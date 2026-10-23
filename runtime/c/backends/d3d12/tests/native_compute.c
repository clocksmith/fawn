/* SPDX-License-Identifier: MIT */
#include "doe_d3d12.h"
#include <stdio.h>
#include <stdlib.h>

/* Actual Windows driver consumption only. Bytecode comes from separately
 * compiled native_compute.hlsl; no embedded interpreter or fallback backend.
 * Build does not run this fixture or claim physical qualification. */
static void report(const char *step, doe_dx_status status, const doe_dx_error *error) {
    fprintf(stderr, "%s: status=%d native=%ld operation=%s\n", step, (int)status,
        (long)error->native_code, error->operation ? error->operation : "none");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: native_compute.exe native_compute.dxil\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || length > 2 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 2;
    }
    unsigned char *bytecode = malloc((size_t)length);
    if (!bytecode) {
        fclose(file);
        return 2;
    }
    size_t received = fread(bytecode, 1, (size_t)length, file);
    int close_result = fclose(file);
    if (received != (size_t)length || close_result) {
        free(bytecode);
        return 2;
    }
    doe_dx_context *context = NULL;
    doe_dx_buffer *upload = NULL, *input = NULL, *output = NULL, *readback = NULL;
    doe_dx_pipeline *pipeline = NULL;
    doe_dx_commands *commands = NULL;
    doe_dx_error error = {0};
    doe_dx_status status = doe_dx_context_create(NULL, &context, &error);
    int result = 1;
    bool submitted = false;
    if (status != DOE_DX_OK) {
        report("context", status, &error);
        goto cleanup;
    }
    doe_dx_caps caps;
    status = doe_dx_context_caps(context, &caps);
    if (status != DOE_DX_OK) goto cleanup;
    printf("adapter=%s vendor=%u device=%u LUID=%ld:%lu software=%d\n", caps.name,
        caps.vendor_id, caps.device_id, (long)caps.luid_high, (unsigned long)caps.luid_low,
        caps.software_adapter ? 1 : 0);
    uint32_t inputs[16], actual[16];
    for (uint32_t index = 0; index < 16; index++) inputs[index] = index * 97u + 3u;
    status = doe_dx_buffer_create(context, sizeof(inputs), DOE_DX_UPLOAD, &upload, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(inputs), DOE_DX_DEFAULT, &input, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(inputs), DOE_DX_DEFAULT, &output, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_create(context, sizeof(inputs), DOE_DX_READBACK, &readback, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_write(upload, 0, inputs, sizeof(inputs), &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_binding_layout layout[2] = {{0, 0, 4, DOE_DX_READ_ONLY}, {0, 0, 4, DOE_DX_READ_WRITE}};
    doe_dx_pipeline_desc description = {bytecode, (size_t)length, layout, 2};
    status = doe_dx_pipeline_create(context, &description, &pipeline, &error);
    if (status != DOE_DX_OK) goto failed;
    free(bytecode);
    bytecode = NULL;
    status = doe_dx_commands_create(context, &commands, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, upload, 0, input, 0, sizeof(inputs), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, upload, 0, output, 0, sizeof(inputs), &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_buffer_binding bindings[2] = {{input, 0, sizeof(inputs)}, {output, 0, sizeof(inputs)}};
    status = doe_dx_commands_dispatch(commands, pipeline, bindings, 2, 1, 1, 1, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, output, 0, readback, 0, sizeof(inputs), &error);
    if (status != DOE_DX_OK) goto failed;
    /* These caller references must not be needed by later native submission. */
    doe_dx_buffer_release(upload);
    upload = NULL;
    doe_dx_buffer_release(input);
    input = NULL;
    doe_dx_buffer_release(output);
    output = NULL;
    doe_dx_pipeline_release(pipeline);
    pipeline = NULL;
    status = doe_dx_commands_submit(commands, &error);
    submitted = status == DOE_DX_OK || status == DOE_DX_COMPLETION_UNKNOWN;
    if (!submitted) goto failed;
    doe_dx_commands_release(commands);
    commands = NULL;
    status = doe_dx_context_poll(context, 30000, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_buffer_read(readback, 0, actual, sizeof(actual), &error);
    if (status != DOE_DX_OK) goto failed;
    for (size_t index = 0; index < 16; index++) {
        uint32_t expected = (inputs[index] ^ 0x13579bdfu) + 7u;
        if (actual[index] != expected) {
            fprintf(stderr, "mismatch[%zu]: expected=%u actual=%u\n", index, expected, actual[index]);
            goto cleanup;
        }
    }
    puts("native D3D12 readback and early caller release passed");
    result = 0;
    goto cleanup;
failed:
    report("native compute", status, &error);
cleanup:
    free(bytecode);
    doe_dx_commands_release(commands);
    doe_dx_pipeline_release(pipeline);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(input);
    doe_dx_buffer_release(output);
    doe_dx_buffer_release(readback);
    if (context) {
        doe_dx_status closed = doe_dx_context_destroy(context);
        if (closed != DOE_DX_OK) {
            fprintf(stderr, "context retained unresolved native work; destruction refused (%d), submitted=%d\n",
                (int)closed, submitted ? 1 : 0);
            result = 1;
        }
    }
    return result;
}
