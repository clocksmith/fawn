/* SPDX-License-Identifier: MIT */
#include "native_fixture.h"

/* Actual Windows driver consumption only. Bytecode comes from separately
 * compiled native_compute.hlsl; no embedded interpreter or fallback backend.
 * Build does not run this fixture or claim physical qualification. */

int main(int argc, char **argv) {
    bool warp = argc > 1 && strcmp(argv[argc - 1], "--warp") == 0;
    int mode_argc = argc - (warp ? 1 : 0);
    if (mode_argc != 2 && mode_argc != 3) {
        fprintf(stderr, "usage: native_compute.exe shader.dxil [native_indirect.dxil | --uniform] [--warp]\n");
        return 2;
    }
    bool uniform_mode = mode_argc == 3 && strcmp(argv[2], "--uniform") == 0;
    bool indirect_mode = mode_argc == 3 && !uniform_mode;
    size_t length = 0;
    unsigned char *bytecode = read_bytecode(argv[1], &length);
    if (!bytecode) return 2;
    size_t producer_length = 0;
    unsigned char *producer_bytecode = indirect_mode ? read_bytecode(argv[2], &producer_length) : NULL;
    if (indirect_mode && !producer_bytecode) {
        free(bytecode);
        return 2;
    }
    doe_dx_context *context = NULL;
    doe_dx_buffer *upload = NULL, *input = NULL, *output = NULL, *readback = NULL;
    doe_dx_buffer *arguments = NULL, *argument_readback = NULL;
    doe_dx_buffer *uniform_upload = NULL, *uniform = NULL;
    doe_dx_pipeline *pipeline = NULL, *producer = NULL;
    doe_dx_commands *commands = NULL;
    doe_dx_error error = {0};
    const doe_dx_options options = {.allow_software_adapter=warp,
        .adapter_selection=warp ? DOE_DX_SELECT_WARP : DOE_DX_SELECT_ORDINAL};
    doe_dx_status status = doe_dx_context_create(&options, &context, &error);
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
    printf("selection=%d ordinal=%lu\n", (int)caps.adapter_selection, (unsigned long)caps.adapter_ordinal);
    if (warp && (caps.adapter_selection != DOE_DX_SELECT_WARP || !caps.software_adapter ||
            caps.adapter_ordinal != DOE_DX_NO_ADAPTER_ORDINAL)) {
        fputs("WARP selection did not return its explicit software identity\n", stderr);
        goto cleanup;
    }
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
    if (indirect_mode) {
        status = doe_dx_buffer_create(context, sizeof(inputs), DOE_DX_DEFAULT, &arguments, &error);
        if (status != DOE_DX_OK) goto failed;
        status = doe_dx_buffer_create(context, sizeof(inputs), DOE_DX_READBACK, &argument_readback, &error);
        if (status != DOE_DX_OK) goto failed;
    }
    status = doe_dx_buffer_write(upload, 0, inputs, sizeof(inputs), &error);
    if (status != DOE_DX_OK) goto failed;
    if (uniform_mode) {
        uint32_t parameters[64] = {0x13579bdfu, 7u};
        status = doe_dx_buffer_create(context, sizeof(parameters), DOE_DX_UPLOAD, &uniform_upload, &error);
        if (status != DOE_DX_OK) goto failed;
        status = doe_dx_buffer_create(context, sizeof(parameters), DOE_DX_DEFAULT, &uniform, &error);
        if (status != DOE_DX_OK) goto failed;
        status = doe_dx_buffer_write(uniform_upload, 0, parameters, sizeof(parameters), &error);
        if (status != DOE_DX_OK) goto failed;
    }
    doe_dx_binding_layout layout[3] = {
        {0, 0, 4, DOE_DX_READ_ONLY, DOE_DX_STRUCTURED},
        {0, 0, 4, DOE_DX_READ_WRITE, DOE_DX_STRUCTURED},
        {0, 0, 0, DOE_DX_READ_ONLY, DOE_DX_UNIFORM}
    };
    size_t binding_count = uniform_mode ? 3u : 2u;
    doe_dx_pipeline_desc description = {bytecode, length, layout, binding_count};
    status = doe_dx_pipeline_create(context, &description, &pipeline, &error);
    if (status != DOE_DX_OK) goto failed;
    free(bytecode);
    bytecode = NULL;
    if (indirect_mode) {
        doe_dx_binding_layout producer_layout = {0, 0, 4, DOE_DX_READ_WRITE, DOE_DX_STRUCTURED};
        doe_dx_pipeline_desc producer_description = {producer_bytecode, producer_length, &producer_layout, 1};
        status = doe_dx_pipeline_create(context, &producer_description, &producer, &error);
        if (status != DOE_DX_OK) goto failed;
        free(producer_bytecode);
        producer_bytecode = NULL;
    }
    status = doe_dx_commands_create(context, &commands, &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, upload, 0, input, 0, sizeof(inputs), &error);
    if (status != DOE_DX_OK) goto failed;
    status = doe_dx_commands_copy(commands, upload, 0, output, 0, sizeof(inputs), &error);
    if (status != DOE_DX_OK) goto failed;
    doe_dx_buffer_binding bindings[3] = {{input, 0, sizeof(inputs)}, {output, 0, sizeof(inputs)}, {uniform, 0, 256}};
    if (uniform_mode) {
        status = doe_dx_commands_copy(commands, uniform_upload, 0, uniform, 0, 256, &error);
        if (status != DOE_DX_OK) goto failed;
    }
    if (indirect_mode) {
        status = doe_dx_commands_copy(commands, upload, 0, arguments, 0, sizeof(inputs), &error);
        if (status != DOE_DX_OK) goto failed;
        doe_dx_buffer_binding producer_binding = {arguments, 0, sizeof(inputs)};
        status = doe_dx_commands_dispatch(commands, producer, &producer_binding, 1, 1, 1, 1, &error);
        if (status != DOE_DX_OK) goto failed;
        doe_dx_compute_resources resources = {bindings, 2, NULL, 0};
        status = doe_dx_commands_dispatch_indirect(commands, pipeline, &resources, arguments, 16, &error);
        if (status != DOE_DX_OK) goto failed;
        status = doe_dx_commands_copy(commands, arguments, 0, argument_readback, 0, sizeof(inputs), &error);
    } else {
        status = doe_dx_commands_dispatch(commands, pipeline, bindings, binding_count, 1, 1, 1, &error);
    }
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
    doe_dx_pipeline_release(producer);
    producer = NULL;
    doe_dx_buffer_release(arguments);
    arguments = NULL;
    doe_dx_buffer_release(uniform_upload);
    uniform_upload = NULL;
    doe_dx_buffer_release(uniform);
    uniform = NULL;
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
    if (indirect_mode) {
        uint32_t actual_arguments[16];
        status = doe_dx_buffer_read(argument_readback, 0, actual_arguments, sizeof(actual_arguments), &error);
        if (status != DOE_DX_OK) goto failed;
        for (size_t index = 0; index < 16; index++) {
            uint32_t expected = index >= 4 && index <= 6 ? 1u : inputs[index];
            if (actual_arguments[index] != expected) {
                fprintf(stderr, "argument mismatch[%zu]: expected=%u actual=%u\n",
                    index, expected, actual_arguments[index]);
                goto cleanup;
            }
        }
    }
    if (indirect_mode) puts("native D3D12 GPU-generated indirect arguments and early caller release passed");
    else if (uniform_mode) puts("native D3D12 uniform binding readback and early caller release passed");
    else puts("native D3D12 readback and early caller release passed");
    if (warp) puts("WARP CPU software execution: no physical GPU qualification");
    result = 0;
    goto cleanup;
failed:
    report("native compute", status, &error);
cleanup:
    free(bytecode);
    free(producer_bytecode);
    doe_dx_commands_release(commands);
    doe_dx_pipeline_release(pipeline);
    doe_dx_pipeline_release(producer);
    doe_dx_buffer_release(upload);
    doe_dx_buffer_release(input);
    doe_dx_buffer_release(output);
    doe_dx_buffer_release(readback);
    doe_dx_buffer_release(arguments);
    doe_dx_buffer_release(argument_readback);
    doe_dx_buffer_release(uniform_upload);
    doe_dx_buffer_release(uniform);
    if (context) {
        doe_dx_status closed = doe_dx_context_destroy(context);
        if (closed != DOE_DX_OK) {
            fprintf(stderr, "context retained unresolved native work; destruction refused (%d), submitted=%d\n",
                (int)closed, submitted ? 1 : 0);
            result = 1;
        } else if (warp && result == 0) puts("WARP context destruction and complete native cleanup passed");
    }
    return result;
}
