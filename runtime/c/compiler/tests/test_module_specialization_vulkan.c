/* SPDX-License-Identifier: MIT
 * One mixed-stage SPIR-V module, two real CPU Vulkan entry-point pipelines. */
#include "doe_vk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void require(doe_vk_status status, const doe_vk_error *error) {
    if (status != DOE_VK_OK) fprintf(stderr, "%s: %s\n", doe_vk_status_name(status), error->operation);
    assert(status == DOE_VK_OK);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    const long bytes = ftell(file);
    assert(bytes > 0 && bytes % 4 == 0 && bytes <= 8192 * 4 && fseek(file, 0, SEEK_SET) == 0);
    uint32_t *words = malloc((size_t)bytes);
    assert(words != NULL && fread(words, 1u, (size_t)bytes, file) == (size_t)bytes && fclose(file) == 0);
    doe_vk_error error = {0};
    doe_vk_context *context = NULL;
    const doe_vk_options options = {.allow_cpu_adapter = true, .adapter_selection = DOE_VK_SELECT_SOFTWARE};
    require(doe_vk_context_create(&options, &context, &error), &error);
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), &error);
    assert(caps.software_adapter);
    const uint32_t expected[] = {7u, 8u, 107u, 108u, 109u};
    uint32_t actual[] = {0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu};
    doe_vk_buffer *buffer = NULL;
    require(doe_vk_buffer_create(context, sizeof(actual), DOE_VK_BUFFER_STORAGE,
        DOE_VK_MEMORY_HOST, &buffer, &error), &error);
    require(doe_vk_buffer_write(buffer, 0u, actual, sizeof(actual)), &error);
    const doe_vk_binding_layout layout = {.group = 0u, .binding = 0u, .kind = DOE_VK_BINDING_STORAGE};
    doe_vk_pipeline *first = NULL, *second = NULL;
    require(doe_vk_pipeline_create(context, words, (size_t)bytes / 4u, "first", &layout, 1u, &first, &error), &error);
    require(doe_vk_pipeline_create(context, words, (size_t)bytes / 4u, "second", &layout, 1u, &second, &error), &error);
    free(words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), &error);
    const doe_vk_buffer_binding binding = {buffer, 0u, sizeof(actual)};
    require(doe_vk_commands_dispatch(commands, first, &binding, 1u, 1u, 1u, 1u), &error);
    require(doe_vk_commands_dispatch(commands, second, &binding, 1u, 1u, 1u, 1u), &error);
    require(doe_vk_commands_submit(commands, &error), &error);
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), &error);
    require(doe_vk_buffer_read(buffer, 0u, actual, sizeof(actual)), &error);
    for (size_t i = 0u; i < sizeof(actual) / sizeof(*actual); ++i) {
        if (actual[i] != expected[i]) fprintf(stderr, "word%zu: %u != %u\n", i, actual[i], expected[i]);
        assert(actual[i] == expected[i]);
    }
    require(doe_vk_commands_release(commands), &error);
    doe_vk_pipeline_release(first);
    doe_vk_pipeline_release(second);
    doe_vk_buffer_release(buffer);
    printf("PASS module specialization CPU Vulkan: %s; both entries from the same mixed-stage module, "
        "independent 2/3 workgroup extents and exact five words; no physical qualification\n", caps.device_name);
    doe_vk_context_release(context);
    return 0;
}
