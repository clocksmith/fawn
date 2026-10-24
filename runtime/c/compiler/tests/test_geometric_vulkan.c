/* SPDX-License-Identifier: MIT
 * Actual CPU Vulkan consumes compiler output and independent exact words. */
#include "geometric_fixture.h"
#include "doe_vk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status, const doe_vk_error *error) {
    if (status != DOE_VK_OK) fprintf(stderr, "%s: %s\n", doe_vk_status_name(status), error->operation);
    assert(status == DOE_VK_OK);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long bytes = ftell(file);
    assert(bytes > 0 && bytes % 4 == 0 && bytes <= 8192 * 4 && !fseek(file, 0, SEEK_SET));
    uint32_t *words = malloc((size_t)bytes);
    assert(words && fread(words, 1, (size_t)bytes, file) == (size_t)bytes && !fclose(file));
    doe_vk_error error = {0};
    doe_vk_context *context = NULL;
    const doe_vk_options options = {.allow_cpu_adapter=true,.adapter_selection=DOE_VK_SELECT_SOFTWARE};
    require(doe_vk_context_create(&options, &context, &error), &error);
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), &error);
    assert(caps.software_adapter);
    doe_vk_buffer *output = NULL;
    require(doe_vk_buffer_create(context, sizeof(geometric_expected), DOE_VK_BUFFER_STORAGE,
        DOE_VK_MEMORY_HOST, &output, &error), &error);
    uint32_t actual[sizeof(geometric_expected)/sizeof(*geometric_expected)];
    memset(actual, 0x5a, sizeof(actual));
    require(doe_vk_buffer_write(output, 0, actual, sizeof(actual)), &error);
    const doe_vk_binding_layout layout = {.group=0,.binding=0,.kind=DOE_VK_BINDING_STORAGE};
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, words, (size_t)bytes / 4, "main", &layout, 1, &pipeline, &error), &error);
    free(words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), &error);
    const doe_vk_buffer_binding binding = {output,0,sizeof(actual)};
    require(doe_vk_commands_dispatch(commands, pipeline, &binding, 1, 1, 1, 1), &error);
    require(doe_vk_commands_submit(commands, &error), &error);
    require(doe_vk_commands_wait(commands, UINT64_C(30000000000), &error), &error);
    require(doe_vk_buffer_read(output, 0, actual, sizeof(actual)), &error);
    for (size_t i = 0; i < sizeof(actual)/sizeof(*actual); ++i) {
        if (actual[i] != geometric_expected[i]) fprintf(stderr, "word%zu: %08x != %08x\n", i, actual[i], geometric_expected[i]);
        assert(actual[i] == geometric_expected[i]);
    }
    require(doe_vk_commands_release(commands), &error);
    doe_vk_pipeline_release(pipeline);
    doe_vk_buffer_release(output);
    printf("PASS geometric CPU Vulkan: %s; exact39 words, faceForward zero/sign roles, refract eta/total-internal/non-unit roles, ordered effects and storage snapshots; no physical qualification\n", caps.device_name);
    doe_vk_context_release(context);
    return 0;
}
