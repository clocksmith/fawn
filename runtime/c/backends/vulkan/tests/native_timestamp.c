/* Real C compiler -> GPU arithmetic and query normalization. Seeded arithmetic
 * fixtures are not query results; actual query results come only from Vulkan. */
#include "../include/doe_vk_timestamp.h"
#include "doe_shader.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status, const char *operation) {
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}
static void *allocate(void *user, size_t bytes, size_t alignment) {
    (void)user;
    return alignment <= _Alignof(max_align_t) ? malloc(bytes) : NULL;
}
static void deallocate(void *user, void *memory, size_t bytes, size_t alignment) {
    (void)user;
    (void)bytes;
    (void)alignment;
    free(memory);
}
static doe_shader *compile(float period, uint32_t bits) {
    char source[DOE_VK_TIMESTAMP_SOURCE_CAPACITY];
    size_t length = 0;
    require(doe_vk_timestamp_shader_source(period, bits, 64, source, sizeof(source), &length), "timestamp source");
    const char identity[] = "developer-timestamp-normalization-integration";
    doe_shader_options options = {
        .allocator = {NULL, allocate, deallocate},
        .capacities = {.typed = {.source_bytes = 8192, .tokens = 4096,
            .statements = 256, .expressions = 1024, .types = 64, .members = 64,
            .resources = 8, .access_steps = 1024, .constants = 128, .constant_edges = 256,
            .ir_instructions = 8192, .ir_operands = 2048, .target_declarations = 1024,
            .traversal_items = 193, .words = 32768, .expression_depth = 64,
            .control_depth = 32, .type_syntax_depth = 32},
            .entries = 8, .entry_inputs = 40, .entry_references = 64},
        .selection = {.mode = DOE_COMPILER_SELECT_NONE},
        .compiler_identity = identity, .compiler_identity_size = sizeof(identity) - 1
    };
    doe_shader *shader = NULL;
    doe_shader_error error;
    doe_shader_status status = doe_shader_create(&options, source, length, &shader, &error);
    if (status != DOE_SHADER_OK) {
        fprintf(stderr, "timestamp WGSL compile status=%d: %s\n", status, error.diagnostic.message);
        exit(1);
    }
    return shader;
}

static const uint64_t input[] = {0, 1, 2, 3, UINT64_C(0xffff), UINT64_C(0x10000),
    UINT64_C(0xffffffff), UINT64_C(0x100000001), UINT64_C(0x123456789abc),
    UINT64_C(0xffffffffffff), UINT64_C(0xffff123456789abc), UINT64_MAX};
/* Independent arbitrary-precision integer fixtures, floor((input & mask)*2.5). */
static const uint64_t fractional[] = {0, 2, 5, 7, UINT64_C(163837), UINT64_C(163840),
    UINT64_C(10737418237), UINT64_C(10737418242), UINT64_C(50039995859670),
    UINT64_C(703687441776637), UINT64_C(50039995859670), UINT64_C(703687441776637)};

static void arithmetic(doe_vk_context *context, float period, uint32_t bits, const uint64_t *expected) {
    doe_shader *shader = compile(period, bits);
    doe_shader_view view;
    if (doe_shader_borrow(shader, &view) != DOE_SHADER_OK) exit(1);
    doe_vk_pipeline *pipeline = NULL;
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_STORAGE};
    require(doe_vk_pipeline_create(context, view.words, view.counts.words, "main", &layout, 1,
        &pipeline, NULL), "arithmetic pipeline");
    doe_shader_release(shader);
    doe_vk_buffer *buffer = NULL;
    require(doe_vk_buffer_create(context, sizeof(input), DOE_VK_BUFFER_STORAGE,
        DOE_VK_MEMORY_HOST, &buffer, NULL), "arithmetic buffer");
    require(doe_vk_buffer_write(buffer, 0, input, sizeof(input)), "arithmetic input");
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, NULL), "arithmetic commands");
    const doe_vk_buffer_binding binding = {buffer, 0, sizeof(input)};
    require(doe_vk_commands_dispatch(commands, pipeline, &binding, 1, 1, 1, 1), "arithmetic dispatch");
    doe_vk_pipeline_release(pipeline);
    require(doe_vk_commands_submit(commands, NULL), "arithmetic submit");
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), NULL), "arithmetic wait");
    uint64_t output[sizeof(input) / sizeof(input[0])];
    require(doe_vk_buffer_read(buffer, 0, output, sizeof(output)), "arithmetic read");
    if (memcmp(output, expected, sizeof(output)) != 0) {
        for (size_t i = 0; i < sizeof(input) / sizeof(input[0]); ++i)
            fprintf(stderr, "arithmetic %zu: %llu expected %llu\n", i,
                (unsigned long long)output[i], (unsigned long long)expected[i]);
        exit(1);
    }
    require(doe_vk_commands_release(commands), "arithmetic retire");
    doe_vk_buffer_release(buffer);
}

/* Test-only independent 32-bit-limb oracle. It reads genuine returned ticks;
 * production normalization never reads or patches timestamps on the CPU. */
static uint64_t expected_ns(uint64_t ticks, float period, uint32_t valid_bits) {
    ticks &= UINT64_MAX >> (64 - valid_bits);
    uint32_t bits;
    memcpy(&bits, &period, sizeof(bits));
    uint32_t exponent = (bits >> 23) & 255;
    uint32_t multiplier = (bits & UINT32_C(0x7fffff)) | (exponent ? UINT32_C(1) << 23 : 0);
    int shift = exponent ? (int)exponent - 150 : -149;
    uint64_t low = (uint64_t)(uint32_t)ticks * multiplier;
    uint64_t high = (ticks >> 32) * multiplier + (low >> 32);
    uint32_t limbs[3] = {(uint32_t)low, (uint32_t)high, (uint32_t)(high >> 32)};
    uint64_t result = 0;
    for (int bit = 0; bit < 64; ++bit) {
        int source = bit - shift;
        if (source >= 0 && source < 96 && (limbs[source / 32] & (UINT32_C(1) << (source % 32))))
            result |= UINT64_C(1) << bit;
    }
    return result;
}

static void actual_queries(doe_vk_context *context, const doe_vk_caps *caps) {
    doe_shader *shader = compile(caps->timestamp_period_ns, caps->timestamp_valid_bits);
    doe_shader_view view;
    if (doe_shader_borrow(shader, &view) != DOE_SHADER_OK) exit(1);
    doe_vk_timestamp_normalizer *normalizer = NULL;
    const doe_vk_timestamp_normalizer_desc desc = {.spirv = view.words,
        .word_count = view.counts.words, .workgroup_size = 64, .capacity = 5,
        .period_ns = caps->timestamp_period_ns, .valid_bits = caps->timestamp_valid_bits};
    require(doe_vk_timestamp_normalizer_create(context, &desc, &normalizer, NULL), "normalizer");
    doe_shader_release(shader);
    doe_vk_query_pool *pool = NULL;
    require(doe_vk_query_pool_create(context, 5, &pool, NULL), "queries");
    doe_vk_buffer *output = NULL;
    require(doe_vk_buffer_create(context, 160, DOE_VK_BUFFER_COPY_DST | DOE_VK_BUFFER_COPY_SRC,
        DOE_VK_MEMORY_HOST, &output, NULL), "query output");
    uint64_t initial[20];
    for (size_t i = 0; i < 20; ++i) initial[i] = UINT64_MAX;
    require(doe_vk_buffer_write(output, 0, initial, sizeof(initial)), "query sentinel");
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, NULL), "query commands");
    require(doe_vk_commands_reset_queries(commands, pool, 0, 5), "query reset");
    require(doe_vk_commands_write_timestamp(commands, pool, 0, DOE_VK_TIMESTAMP_BEGIN), "query begin");
    require(doe_vk_commands_fill(commands, output, 0, 8, 0), "real intervening fill");
    require(doe_vk_commands_write_timestamp(commands, pool, 3, DOE_VK_TIMESTAMP_END), "query end");
    require(doe_vk_commands_resolve_queries(commands, pool, 0, 5, output, 0), "raw observation");
    require(doe_vk_commands_resolve_queries_ns(commands, pool, 0, 5, normalizer, output, 40), "nanoseconds");
    require(doe_vk_commands_copy(commands, output, 40, output, 80, 40), "same-batch nanosecond copy");
    /* Reuse the same scratch with a shorter exact binding; preserve adjacent bytes. */
    require(doe_vk_commands_resolve_queries_ns(commands, pool, 3, 2, normalizer, output, 120), "scratch reuse");
    doe_vk_timestamp_normalizer_release(normalizer);
    doe_vk_query_pool_release(pool);
    require(doe_vk_commands_submit(commands, NULL), "query submit");
    if (doe_vk_commands_release(commands) != DOE_VK_BUSY) exit(1);
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), NULL), "query complete");
    uint64_t values[20];
    require(doe_vk_buffer_read(output, 0, values, sizeof(values)), "query read");
    for (size_t i = 0; i < 5; ++i) {
        uint64_t expected = expected_ns(values[i], caps->timestamp_period_ns, caps->timestamp_valid_bits);
        if (values[5 + i] != expected || values[10 + i] != expected) exit(1);
        if (i != 0 && i != 3 && values[i] != 0) exit(1);
    }
    if (values[15] != values[8] || values[16] != 0 || values[17] != UINT64_MAX ||
        values[18] != UINT64_MAX || values[19] != UINT64_MAX) exit(1);
    require(doe_vk_commands_release(commands), "query retire");
    doe_vk_buffer_release(output);
}

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--allow-software") != 0)) return 2;
    doe_vk_context *context = NULL;
    doe_vk_error error;
    const doe_vk_options options = {.allow_cpu_adapter = argc == 2};
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP timestamp integration: %s at %s native=%d\n",
            doe_vk_status_name(status), error.operation, error.native_result);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    printf("Timestamp adapter: %s software=%u period=%g valid_bits=%u\n",
        caps.device_name, caps.software_adapter, (double)caps.timestamp_period_ns, caps.timestamp_valid_bits);
    arithmetic(context, 2.5f, 48, fractional);
    uint64_t zeros[sizeof(input) / sizeof(input[0])] = {0};
    uint32_t smallest_bits = 1;
    float subnormal;
    memcpy(&subnormal, &smallest_bits, sizeof(subnormal));
    arithmetic(context, subnormal, 64, zeros);
    arithmetic(context, FLT_MAX, 64, zeros);
    arithmetic(context, 1.0f, 64, input);
    actual_queries(context, &caps);
    doe_vk_context_release(context);
    puts("PASS C-compiled GPU fractional/masked/subnormal/overflow/identity arithmetic fixtures, genuine query nanoseconds, same-batch copy, scratch reuse, zero slots and early normalizer release; software is not physical qualification");
    return 0;
}
