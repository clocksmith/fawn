/* SPDX-License-Identifier: MIT
 * Actual explicit CPU Vulkan executes DXC's translation of fresh emitted HLSL. */
#include "doe_vk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status) { assert(status == DOE_VK_OK); }
/* The runtime size table: one uint per canonical resource, the bound range bytes. */
static doe_vk_buffer *sizes(doe_vk_context *context, uint32_t first, uint32_t second) {
    doe_vk_buffer *table = NULL;
    const uint32_t rows[64] = {first, second};
    require(doe_vk_buffer_create(context, sizeof(rows), DOE_VK_BUFFER_UNIFORM, DOE_VK_MEMORY_HOST, &table, NULL));
    require(doe_vk_buffer_write(table, 0, rows, sizeof(rows)));
    return table;
}
static void run(doe_vk_context *context, doe_vk_pipeline *pipeline,
    const doe_vk_buffer_binding *bindings, size_t count) {
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, NULL));
    require(doe_vk_commands_dispatch(commands, pipeline, bindings, count, 1, 1, 1));
    require(doe_vk_commands_submit(commands, NULL));
    require(doe_vk_commands_wait(commands, UINT64_C(30000000000), NULL));
    require(doe_vk_commands_release(commands));
}
static void operations(doe_vk_context *context, doe_vk_pipeline *pipeline, bool shared, bool signed_value) {
    /* Independent old-value, wrap, signed-min/max, exchange-success/failure and
     * final-value witnesses; no HLSL or WGSL formula computes these words. */
    static const uint32_t signed_expected[14] = {10,0xfffffff9,0xfffffffc,0xfffffffa,5,
        0xfffffffb,3,7,1,9,1,11,0,11};
    static const uint32_t unsigned_expected[14] = {10,0xfffffffd,0,0xfffffffe,0xfffffffe,
        9,1,5,3,9,1,11,0,11};
    doe_vk_buffer *cells = NULL, *output = NULL;
    require(doe_vk_buffer_create(context, 16, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &cells, NULL));
    require(doe_vk_buffer_create(context, 64, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &output, NULL));
    doe_vk_buffer *table = sizes(context, 8, 64);
    const doe_vk_buffer_binding bindings[3] = {{cells,0,8},{output,0,64},{table,0,256}};
    const doe_vk_buffer_binding shared_bindings[2] = {{output,0,64},{table,0,256}};
    const int32_t indices[] = {0,1,-1,2,INT32_MAX};
    for (size_t at = 0; at < sizeof(indices)/sizeof(*indices); ++at) {
        const uint32_t initial[4] = {10,10,0xa5a5a5a5,0x5a5a5a5a};
        uint32_t words[16];
        for (size_t i = 0; i < 16; ++i) words[i] = 0x12345678;
        memcpy(&words[15], &indices[at], 4);
        require(doe_vk_buffer_write(cells, 0, initial, sizeof(initial)));
        require(doe_vk_buffer_write(output, 0, words, sizeof(words)));
        run(context, pipeline, shared ? shared_bindings : bindings, shared ? 2 : 3);
        require(doe_vk_buffer_read(output, 0, words, sizeof(words)));
        /* Storage indices clamp to the last bound cell (1): -1, 2 and INT32_MAX
         * act on cell 1 exactly as index 1 does. Workgroup indices keep their
         * guard: out-of-range operations read zero and leave memory untouched. */
        const size_t cell = at < 2 ? at : 1;
        for (size_t i = 0; i < 14; ++i) {
            uint32_t expected = shared && at >= 2 ? 0 : (signed_value ? signed_expected : unsigned_expected)[i];
            if (shared && i == 0) expected = 0;
            if (words[i] != expected) fprintf(stderr, "index%d word%zu: %08x != %08x\n", indices[at], i, words[i], expected);
            assert(words[i] == expected);
        }
        assert(words[14] == 0x12345678);
        uint32_t index;
        memcpy(&index, &indices[at], 4);
        assert(words[15] == index);
        uint32_t actual[4];
        require(doe_vk_buffer_read(cells, 0, actual, sizeof(actual)));
        for (size_t i = 0; i < 4; ++i)
            assert(actual[i] == (!shared && i == cell ? 11 : initial[i]));
    }
    doe_vk_buffer_release(table);
    doe_vk_buffer_release(output);
    doe_vk_buffer_release(cells);
}
static void effects(doe_vk_context *context, doe_vk_pipeline *pipeline) {
    doe_vk_buffer *data = NULL, *cursor = NULL;
    require(doe_vk_buffer_create(context, 24, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &data, NULL));
    require(doe_vk_buffer_create(context, 4, DOE_VK_BUFFER_STORAGE, DOE_VK_MEMORY_HOST, &cursor, NULL));
    for (size_t bytes = 16; bytes <= 24; bytes += 8) {
        const uint32_t initial[6] = {91,92,93,94,0,0x5a5a5a5a}, zero = 0;
        require(doe_vk_buffer_write(data, 0, initial, sizeof(initial)));
        require(doe_vk_buffer_write(cursor, 0, &zero, sizeof(zero)));
        /* Sixteen bytes leave the tail empty: index 0 clamps to offset 16, past
         * the binding, where robust buffer access drops the atomic. */
        doe_vk_buffer *table = sizes(context, (uint32_t)bytes, 4);
        const doe_vk_buffer_binding bindings[3] = {{data,0,bytes},{cursor,0,4},{table,0,256}};
        run(context, pipeline, bindings, 3);
        doe_vk_buffer_release(table);
        uint32_t actual[6], position;
        require(doe_vk_buffer_read(data, 0, actual, sizeof(actual)));
        require(doe_vk_buffer_read(cursor, 0, &position, sizeof(position)));
        assert(position == 3);
        for (size_t i = 0; i < 6; ++i) assert(actual[i] == (i == 4 && bytes == 24 ? 1 : initial[i]));
    }
    doe_vk_buffer_release(cursor);
    doe_vk_buffer_release(data);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long bytes = ftell(file);
    assert(bytes > 0 && bytes % 4 == 0 && !fseek(file, 0, SEEK_SET));
    uint32_t *words = malloc((size_t)bytes);
    assert(words && fread(words, 1, (size_t)bytes, file) == (size_t)bytes && !fclose(file));
    doe_vk_context *context = NULL;
    const doe_vk_options options = {.allow_cpu_adapter=true,.adapter_selection=DOE_VK_SELECT_SOFTWARE};
    require(doe_vk_context_create(&options, &context, NULL));
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps));
    assert(caps.software_adapter);
    const bool shared = strstr(argv[2], "shared") != NULL;
    const doe_vk_binding_layout layouts[3] = {{0,0,DOE_VK_BINDING_STORAGE},{0,1,DOE_VK_BINDING_STORAGE},
        {0,2,DOE_VK_BINDING_UNIFORM}};
    doe_vk_pipeline *pipeline = NULL;
    require(doe_vk_pipeline_create(context, words, (size_t)bytes / 4, "main", shared ? layouts + 1 : layouts,
        shared ? 2 : 3, &pipeline, NULL));
    free(words);
    if (!strcmp(argv[2], "effects")) effects(context, pipeline);
    else operations(context, pipeline, shared, !strncmp(argv[2], "signed_", 7));
    doe_vk_pipeline_release(pipeline);
    printf("PASS emitted HLSL via DXC/SPIR-V on explicit CPU %s: %s; no D3D/physical qualification\n", caps.device_name, argv[2]);
    doe_vk_context_release(context);
    return 0;
}
