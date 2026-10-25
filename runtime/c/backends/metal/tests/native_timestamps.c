/* SPDX-License-Identifier: MIT
 * Authorized physical counter ordering check; no clock-unit claim. */
#define _POSIX_C_SOURCE 200809L
#include "doe_metal_query.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check(doe_mtl_status status, const doe_mtl_error *error) {
    if (status == DOE_MTL_OK) return;
    fprintf(stderr, "timestamp check: %s operation=%s native=%" PRId64 " %s\n",
        doe_mtl_status_name(status), error->operation != NULL ? error->operation : "none",
        error->native_code, error->message);
    exit(1);
}
static bool complete(doe_mtl_commands *commands, doe_mtl_error *error) {
    struct timespec start, now, pause = {0, 1000000};
    doe_mtl_status status;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) return false;
    for (;;) {
        status = doe_mtl_commands_poll(commands, error);
        if (status != DOE_MTL_PENDING && status != DOE_MTL_COMPLETION_UNKNOWN) {
            check(status, error);
            return true;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec - start.tv_sec >= 5) return false;
        (void)nanosleep(&pause, NULL);
    }
}
int main(int argc, char **argv) {
    static const char source[] = "#include <metal_stdlib>\nusing namespace metal;"
        "kernel void stamp(device uint*o[[buffer(0)]],uint i[[thread_position_in_grid]]){o[i]=i+1u;}";
    doe_mtl_context *context = NULL;
    doe_mtl_pipeline *pipeline = NULL;
    doe_mtl_buffer *output = NULL, *results = NULL;
    doe_mtl_timestamps *timestamps = NULL;
    doe_mtl_error error = {0};
    doe_mtl_caps caps = {0};
    doe_mtl_pipeline_desc desc = {source, sizeof(source) - 1, "stamp", {1, 1, 1}, UINT32_MAX};
    uint64_t previous = 0;
    if (argc != 2 || strcmp(argv[1], "--run") != 0 || getenv("DOE_MTL_ALLOW_HARDWARE") == NULL ||
        strcmp(getenv("DOE_MTL_ALLOW_HARDWARE"), "1") != 0) return 2;
    check(doe_mtl_context_create(&context, &error), &error);
    check(doe_mtl_context_caps(context, &caps), &error);
    printf("Metal timestamp device: %s; raw stage counters, GPU resolve\n", caps.device_name);
    assert(caps.timestamp_stage);
    check(doe_mtl_pipeline_create(context, &desc, &pipeline, &error), &error);
    check(doe_mtl_buffer_create(context, 4, DOE_MTL_SHARED, &output, &error), &error);
    check(doe_mtl_buffer_create(context, 272, DOE_MTL_SHARED, &results, &error), &error);
    check(doe_mtl_timestamps_create(context, 2, &timestamps, &error), &error);
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        doe_mtl_commands *commands = NULL;
        uint64_t first[2] = {0}, second[2] = {0};
        uint32_t value = 0;
        doe_mtl_buffer_binding binding = {0, output, 0, 4};
        doe_mtl_compute_dispatch dispatch = {.pipeline=pipeline, .buffers=&binding, .buffer_count=1,
            .groups={1,1,1}, .repeat=1, .timestamps=timestamps, .timestamp_begin=0, .timestamp_end=1};
        check(doe_mtl_commands_create(context, &commands, &error), &error);
        check(doe_mtl_commands_compute(commands, &dispatch, &error), &error);
        check(doe_mtl_commands_resolve_timestamps(commands, timestamps, 0, 2, results, 0, &error), &error);
        check(doe_mtl_commands_resolve_timestamps(commands, timestamps, 0, 2, results, 256, &error), &error);
        check(doe_mtl_commands_submit(commands, &error), &error);
        if (!complete(commands, &error)) {
            fputs("Unknown GPU completion; all command leases retained\n", stderr);
            return 3;
        }
        check(doe_mtl_buffer_read(output, 0, &value, sizeof(value)), &error);
        check(doe_mtl_buffer_read(results, 0, first, sizeof(first)), &error);
        check(doe_mtl_buffer_read(results, 256, second, sizeof(second)), &error);
        assert(value == 1 && first[0] > previous && first[1] >= first[0] && first[1] != UINT64_MAX);
        assert(memcmp(first, second, sizeof(first)) == 0);
        previous = first[1];
        printf("iteration %u: begin=%" PRIu64 " end=%" PRIu64 " identical GPU resolves\n",
            iteration, first[0], first[1]);
        check(doe_mtl_commands_release(commands), &error);
    }
    doe_mtl_timestamps_release(timestamps);
    doe_mtl_buffer_release(results);
    doe_mtl_buffer_release(output);
    doe_mtl_pipeline_release(pipeline);
    doe_mtl_context_release(context);
    return 0;
}
