/* SPDX-License-Identifier: MIT
 * Real timestamp consumption; software ICD only, not a timing benchmark. */
#include "vulkan_fixture.h"
#include "doe_vk_queries.h"
#include <math.h>

static void release_queries(void *state, void *pool)
{
    ((lifecycle *)state)->native_releases++;
    doe_vk_query_pool_release(pool);
}

int main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--allow-software")) return 2;
    doe_vk_options options = {.allow_cpu_adapter = true};
    doe_vk_context *context;
    doe_vk_error error = {0};
    require(doe_vk_context_create(&options, &context, &error), "create timestamp context");
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    if (!caps.software_adapter) {
        doe_vk_context_release(context);
        fputs("Refusing non-software adapter in software-only developer check\n", stderr);
        return 77;
    }
    if (!caps.timestamp_valid_bits || caps.timestamp_valid_bits > 64 ||
        !isfinite(caps.timestamp_period_ns) || caps.timestamp_period_ns <= 0) {
        doe_vk_context_release(context);
        fputs("SKIP selected queue has no usable timestamp capability\n", stderr);
        return 77;
    }
    printf("Resource timestamp consumer: %s, bits=%u, period_ns=%g; software only, no performance qualification\n",
        caps.device_name, caps.timestamp_valid_bits, caps.timestamp_period_ns);
    lifecycle life = {0};
    doe_resource_config config = {.allocator = {&life, NULL, allocate, deallocate},
        .device = {context, alive, retain, release}};
    doe_vk_query_pool *native_pool;
    require(doe_vk_query_pool_create(context, 5, &native_pool, &error), "pool");
    doe_resource_query_set *queries;
    assert(doe_resource_query_set_create(&config, WGPUQueryType_Timestamp, 5,
        (doe_resource_native){native_pool, &life, release_queries}, &queries) == DOE_RESOURCE_OK);
    assert(doe_resource_query_set_device(queries) == context);
    assert(doe_resource_query_set_type(queries) == WGPUQueryType_Timestamp);
    doe_vk_buffer *native_result;
    require(doe_vk_buffer_create(context, 512, DOE_VK_BUFFER_COPY_DST,
        DOE_VK_MEMORY_HOST, &native_result, &error), "result");
    uint8_t sentinel[512];
    memset(sentinel, 0xa5, sizeof(sentinel));
    require(doe_vk_buffer_write(native_result, 0, sentinel, sizeof(sentinel)), "sentinel");
    doe_resource_buffer_desc result_desc = {512, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopyDst, false};
    doe_resource_buffer *result;
    assert(doe_resource_buffer_create(&config, &result_desc,
        (doe_resource_native){native_result, &life, release_buffer}, &result) == DOE_RESOURCE_OK);
    doe_resource_encoder *encoder;
    assert(doe_resource_encoder_create(&config, &encoder) == DOE_RESOURCE_OK);
    /* WebGPU alignment remains256 even though packed native ticks need only8. */
    assert(doe_resource_encoder_resolve_queries(encoder, queries, 0, 1, result, 8) == DOE_RESOURCE_ALIGNMENT);
    doe_resource_encoder_release(encoder);
    assert(doe_resource_encoder_create(&config, &encoder) == DOE_RESOURCE_OK);
    doe_resource_compute_pass *pass;
    doe_resource_pass_timestamps timestamps = {queries, 0, 1};
    assert(doe_resource_encoder_begin_compute_with_timestamps(encoder, &timestamps, &pass) == DOE_RESOURCE_OK);
    assert(doe_resource_compute_pass_end(pass) == DOE_RESOURCE_OK);
    doe_resource_compute_pass_release(pass);
    assert(doe_resource_encoder_clear_buffer(encoder, result, 0, 16) == DOE_RESOURCE_OK);
    assert(doe_resource_encoder_write_timestamp(encoder, queries, 3, DOE_TIMESTAMP_COMMAND) == DOE_RESOURCE_OK);
    /* Slots2/4 are unwritten and must resolve zero, never WAIT on unavailable. */
    assert(doe_resource_encoder_resolve_queries(encoder, queries, 0, 5, result, 256) == DOE_RESOURCE_OK);
    doe_resource_commands *commands;
    assert(doe_resource_encoder_finish(encoder, &commands) == DOE_RESOURCE_OK);
    doe_resource_encoder_release(encoder);
    doe_resource_query_set_release(queries);
    doe_resource_buffer_release(result);
    assert(!life.native_releases);
    doe_vk_commands *native_commands;
    require(doe_vk_commands_create(context, &native_commands, &error), "commands");
    doe_vk_context_release(context);
    size_t count;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    assert(count == 5);
    /* Reset is the native availability owner's concern, not a public recording
     * state clone. This consumer resets its exact one newly-created pool once. */
    require(doe_vk_commands_reset_queries(native_commands, native_pool, 0, 5), "reset new pool");
    for (size_t i = 0; i < count; i++) {
        const doe_resource_command *record = &records[i];
        if (record->kind == DOE_COMMAND_WRITE_TIMESTAMP) {
            /* Match retained pass-begin TOP and pass-end/command BOTTOM semantics. */
            require(doe_vk_commands_write_timestamp(native_commands,
                doe_resource_query_set_native(record->value.timestamp.set), record->value.timestamp.index,
                record->value.timestamp.position == DOE_TIMESTAMP_PASS_BEGIN ? DOE_VK_TIMESTAMP_BEGIN : DOE_VK_TIMESTAMP_END),
                "write owned timestamp");
        } else if (record->kind == DOE_COMMAND_CLEAR_BUFFER) {
            require(doe_vk_commands_fill(native_commands, doe_resource_buffer_native(record->value.clear.buffer),
                record->value.clear.offset, record->value.clear.size, 0), "clear owned payload");
        } else if (record->kind == DOE_COMMAND_RESOLVE_QUERY) {
            require(doe_vk_commands_resolve_queries(native_commands,
                doe_resource_query_set_native(record->value.resolve.set), record->value.resolve.first,
                record->value.resolve.count, doe_resource_buffer_native(record->value.resolve.destination),
                record->value.resolve.offset), "resolve owned timestamp span");
        } else abort();
    }
    assert(doe_resource_commands_begin_submit(commands) == DOE_RESOURCE_OK);
    doe_resource_commands_release(commands);
    require(doe_vk_commands_submit(native_commands, &error), "submit");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_PENDING) == DOE_RESOURCE_PENDING);
    assert(doe_vk_commands_release(native_commands) == DOE_VK_BUSY);
    uint64_t ticks[5];
    assert(doe_vk_buffer_read(native_result, 256, ticks, sizeof(ticks)) == DOE_VK_BUSY);
    assert(!life.native_releases);
    require(doe_vk_commands_wait(native_commands, UINT64_C(5000000000), &error), "wait");
    assert(doe_resource_commands_settle(commands, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
    assert(life.native_releases == 2 && !life.allocations);
    require(doe_vk_buffer_read(native_result, 256, ticks, sizeof(ticks)), "read physical ticks after resource finalization");
    uint8_t payload[512];
    require(doe_vk_buffer_read(native_result, 0, payload, sizeof(payload)), "check exact neighboring payload");
    for (size_t i = 0; i < sizeof(payload); i++) {
        if (i >= 256 && i < 296) continue;
        assert(payload[i] == (i < 16 ? 0 : 0xa5));
    }
    assert(ticks[2] == 0 && ticks[4] == 0);
    const uint64_t unwritten_sentinel = UINT64_C(0xa5a5a5a5a5a5a5a5);
    assert(ticks[0] != unwritten_sentinel && ticks[1] != unwritten_sentinel && ticks[3] != unwritten_sentinel);
    uint64_t mask = caps.timestamp_valid_bits == 64 ? UINT64_MAX : (UINT64_C(1) << caps.timestamp_valid_bits) - 1;
    printf("Observed raw device ticks: %llu,%llu,%llu; wrap-aware spans=%llu,%llu (not elapsed-time claims)\n",
        (unsigned long long)ticks[0], (unsigned long long)ticks[1], (unsigned long long)ticks[3],
        (unsigned long long)((ticks[1] - ticks[0]) & mask), (unsigned long long)((ticks[3] - ticks[1]) & mask));
    require(doe_vk_commands_release(native_commands), "retire native query owner");
    puts("PASS resource timed-pass/command timestamps, real reset/write/resolve, two unwritten zeros, exact payload, pending leases and terminal finalizers");
    return 0;
}
