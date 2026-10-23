/* SPDX-License-Identifier: MIT
 * Explicit bounded Metal compute/copy/readback smoke, never run by make check. */
#define _POSIX_C_SOURCE 200809L
#include "doe_metal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { ELEMENTS = 64, DEADLINE_SECONDS = 5 };
static const char source[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "kernel void doe_smoke(device uint *data [[buffer(0)]], "
    "uint index [[thread_position_in_grid]]) {\n"
    "  if (index < 64u) data[index] = data[index] * 3u + 7u;\n"
    "}\n";

static int failure(doe_mtl_status status, const doe_mtl_error *error) {
    fprintf(stderr, "Metal native smoke: %s, operation=%s, native=%" PRId64 ", detail=%s\n",
        doe_mtl_status_name(status), error->operation != NULL ? error->operation : "none",
        error->native_code, error->message);
    return 1;
}

static doe_mtl_status await_completion(doe_mtl_commands *commands, doe_mtl_error *error) {
    struct timespec start, now, pause = {0, 1000000};
    doe_mtl_status status;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) return DOE_MTL_COMPLETION_UNKNOWN;
    for (;;) {
        status = doe_mtl_commands_poll(commands, error);
        if (status != DOE_MTL_PENDING && status != DOE_MTL_COMPLETION_UNKNOWN) return status;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
            now.tv_sec - start.tv_sec >= DEADLINE_SECONDS) return DOE_MTL_COMPLETION_UNKNOWN;
        (void)nanosleep(&pause, NULL);
    }
}

int main(int argc, char **argv) {
    doe_mtl_context *context = NULL;
    doe_mtl_buffer *input = NULL, *work = NULL, *output = NULL;
    doe_mtl_pipeline *pipeline = NULL;
    doe_mtl_commands *commands = NULL;
    doe_mtl_error error = {0};
    doe_mtl_caps caps = {0};
    doe_mtl_status status = DOE_MTL_OK;
    uint32_t values[ELEMENTS], actual[ELEMENTS];
    doe_mtl_pipeline_desc desc = {source, sizeof(source) - 1, "doe_smoke", {8, 1, 1}, UINT32_MAX};
    doe_mtl_buffer_binding binding = {0};
    bool submitted = false;
    int result = 1;
    if (argc != 2 || strcmp(argv[1], "--run") != 0 ||
        getenv("DOE_MTL_ALLOW_HARDWARE") == NULL ||
        strcmp(getenv("DOE_MTL_ALLOW_HARDWARE"), "1") != 0) {
        fputs("Explicit authorized run required: DOE_MTL_ALLOW_HARDWARE=1 native_compute --run\n", stderr);
        return 2;
    }
    for (uint32_t i = 0; i < ELEMENTS; ++i) values[i] = i * 17u + 3u;
    status = doe_mtl_context_create(&context, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_context_caps(context, &caps);
    if (status != DOE_MTL_OK) goto cleanup;
    printf("Metal native device: %s; shared/private copy + 64 uint compute + readback\n", caps.device_name);
    status = doe_mtl_buffer_create(context, sizeof(values), DOE_MTL_SHARED, &input, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_create(context, sizeof(values), DOE_MTL_PRIVATE, &work, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_create(context, sizeof(values), DOE_MTL_SHARED, &output, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_write(input, 0, values, sizeof(values));
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_pipeline_create(context, &desc, &pipeline, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_commands_create(context, &commands, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_commands_fill(commands, work, 0, sizeof(values), 0, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_commands_copy(commands, input, 0, work, 0, sizeof(values), &error);
    if (status != DOE_MTL_OK) goto cleanup;
    binding = (doe_mtl_buffer_binding){0, work, 0, sizeof(values)};
    status = doe_mtl_commands_dispatch(commands, pipeline, &binding, 1,
        (doe_mtl_dimensions){8, 1, 1}, 1, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_commands_copy(commands, work, 0, output, 0, sizeof(values), &error);
    if (status != DOE_MTL_OK) goto cleanup;
    memset(actual, 0xa5, sizeof(actual));
    if (doe_mtl_buffer_read(output, 0, actual, sizeof(actual)) != DOE_MTL_BUSY) {
        fputs("Metal native smoke: recorded output was readable before completion\n", stderr);
        goto cleanup;
    }
    for (size_t i = 0; i < sizeof(actual); ++i) {
        if (((const unsigned char *)actual)[i] != 0xa5) {
            fputs("Metal native smoke: rejected read modified output\n", stderr);
            goto cleanup;
        }
    }
    /* Recording leases, not caller references, cover actual unretained commands. */
    doe_mtl_buffer_release(input);
    input = NULL;
    doe_mtl_buffer_release(work);
    work = NULL;
    doe_mtl_pipeline_release(pipeline);
    pipeline = NULL;
    doe_mtl_context_release(context);
    context = NULL;
    status = doe_mtl_commands_submit(commands, &error);
    if (status != DOE_MTL_OK) goto cleanup;
    submitted = true;
    status = await_completion(commands, &error);
    if (status == DOE_MTL_COMPLETION_UNKNOWN) {
        fputs("Metal native smoke: deadline/unknown completion; owned resources not released\n", stderr);
        return 3;
    }
    submitted = false;
    if (status != DOE_MTL_OK) goto cleanup;
    status = doe_mtl_buffer_read(output, 0, actual, sizeof(actual));
    if (status != DOE_MTL_OK) goto cleanup;
    for (uint32_t i = 0; i < ELEMENTS; ++i) {
        const uint32_t expected = values[i] * 3u + 7u;
        if (actual[i] != expected) {
            fprintf(stderr, "Metal native smoke: element %u, observed %u, expected %u\n", i, actual[i], expected);
            goto cleanup;
        }
    }
    puts("Metal native smoke: exact 64-element readback and early caller-release lifetime pass");
    result = 0;
cleanup:
    if (status != DOE_MTL_OK) (void)failure(status, &error);
    if (submitted) {
        fputs("Metal native smoke: unresolved submission; owned resources not released\n", stderr);
        return 3;
    }
    if (doe_mtl_commands_release(commands) != DOE_MTL_OK) return 3;
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_buffer_release(input);
    doe_mtl_buffer_release(work);
    doe_mtl_buffer_release(output);
    doe_mtl_context_release(context);
    return result;
}
