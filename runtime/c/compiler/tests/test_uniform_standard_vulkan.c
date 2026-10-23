/* SPDX-License-Identifier: MIT
 * Baseline Vulkan consumes emitted compact uniform bytes with exact descriptor
 * ranges. A well-typed wrong-offset module must produce a distinguishable value. */
#include "uniform_standard_fixture.h"
#include "doe_vk.h"
#include <stdio.h>

static void require(doe_vk_status status) {
    if (status != DOE_VK_OK) fprintf(stderr, "Vulkan: %s\n", doe_vk_status_name(status));
    assert(status == DOE_VK_OK);
}
static uint32_t *read_words(const char *directory, const char *name, size_t *count) {
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s.spv", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long bytes = ftell(file);
    assert(bytes > 0 && bytes % 4 == 0 && bytes <= 65536 * 4 && !fseek(file, 0, SEEK_SET));
    uint32_t *words = malloc((size_t)bytes);
    assert(words && fread(words, 1, (size_t)bytes, file) == (size_t)bytes && !fclose(file));
    *count = (size_t)bytes / 4;
    return words;
}
static void run(doe_vk_context *context, const char *directory, size_t index, bool mutant) {
    const uniform_case *test = &uniform_cases[index];
    uint32_t data[16] = {3,5,7,11,13,17,19,23,29,UINT32_C(0xeeeeeeee),31,37,
        UINT32_C(0xaaaaaaaa),UINT32_C(0xbbbbbbbb),41,UINT32_C(0xcccccccc)};
    if (index == 4) {
        const float values[] = {3,5,7,11,13,17};
        memcpy(data, values, sizeof(values));
        data[6] = 40;
    }
    size_t count;
    uint32_t *words = read_words(directory, mutant ? "tail12-wrong-offset" : test->name, &count);
    doe_vk_error error;
    doe_vk_buffer *uniform = NULL, *output = NULL;
    require(doe_vk_buffer_create(context, test->bytes, DOE_VK_BUFFER_UNIFORM, DOE_VK_MEMORY_HOST, &uniform, &error));
    require(doe_vk_buffer_write(uniform, 0, data, test->bytes));
    const uint32_t output_bytes = index == 2 ? 20u : 16u;
    require(doe_vk_buffer_create(context, output_bytes, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &output, &error));
    uint32_t initial[5] = {999,999,999,999,999};
    if (index < 3) {
        initial[1] = UINT32_MAX; /* Dynamic signed -1 after i32 conversion. */
        initial[2] = (uint32_t)index + 1u; /* Exact array count, first invalid index. */
        initial[3] = UINT32_MAX; /* Dynamic high unsigned index. */
    }
    require(doe_vk_buffer_write(output, 0, initial, output_bytes));
    const doe_vk_binding_layout layout[2] = {{.group=0,.binding=0,.kind=DOE_VK_BINDING_UNIFORM},
        {.group=0,.binding=1,.kind=DOE_VK_BINDING_STORAGE}};
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, words, count, "main", layout, 2, &pipeline, &error));
    free(words);
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error));
    const doe_vk_buffer_binding bindings[2] = {{uniform,0,test->bytes},{output,0,output_bytes}};
    require(doe_vk_commands_dispatch(commands, pipeline, bindings, 2, 1, 1, 1));
    require(doe_vk_commands_submit(commands, &error));
    require(doe_vk_commands_wait(commands, UINT64_C(30000000000), &error));
    uint32_t actual[5];
    require(doe_vk_buffer_read(output, 0, actual, output_bytes));
    if (index == 2) assert(actual[4] == 7); /* Final scalar lane at byte8, ending exactly at byte12. */
    assert(actual[0] == (mutant ? 7u : test->expected));
    if (mutant) assert(actual[0] != test->expected);
    if (index == 3) assert(actual[1] == 0 && actual[2] == 23 && actual[3] == 41);
    else if (index < 3) assert(actual[1] == 0 && actual[2] == 0 && actual[3] == 0);
    else assert(actual[1] == 999 && actual[2] == 999 && actual[3] == 999);
    require(doe_vk_commands_release(commands));
    doe_vk_pipeline_release(pipeline);
    doe_vk_buffer_release(output);
    doe_vk_buffer_release(uniform);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    doe_vk_context *context = NULL;
    doe_vk_error error;
    const doe_vk_options options = {.allow_cpu_adapter=true,.adapter_selection=DOE_VK_SELECT_SOFTWARE};
    require(doe_vk_context_create(&options, &context, &error));
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps));
    assert(caps.software_adapter);
    for (size_t i = 0; i < sizeof(uniform_cases) / sizeof(uniform_cases[0]); ++i) run(context, argv[1], i, false);
    run(context, argv[1], 2, true);
    printf("PASS standard uniform CPU Vulkan: %s; exact 4/8/12/32/64-byte descriptors, nested arrays/structs/attributes, "
        "whole aggregates, negative index, narrow matrix and well-typed wrong-offset control; no physical qualification\n", caps.device_name);
    doe_vk_context_release(context);
}
