#include "../include/doe_vk_queries.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(doe_vk_status status, const char *operation) {
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "%s: %s\n", operation, doe_vk_status_name(status));
    exit(1);
}
int main(int argc, char **argv) {
    if (argc > 2) return 2;
    doe_vk_options options = {.allow_cpu_adapter = argc == 2 && strcmp(argv[1], "--allow-software") == 0};
    doe_vk_context *context = NULL;
    doe_vk_error error;
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP timestamp queries: %s at %s native=%d\n", doe_vk_status_name(status), error.operation, error.native_result);
        return status == DOE_VK_UNSUPPORTED || status == DOE_VK_LOADER_UNAVAILABLE ? 77 : 1;
    }
    doe_vk_caps caps;
    require(doe_vk_context_caps(context, &caps), "caps");
    doe_vk_query_pool *pool = NULL;
    status = doe_vk_query_pool_create(context, 4, &pool, &error);
    if (status == DOE_VK_UNSUPPORTED) {
        doe_vk_context_release(context);
        puts("SKIP selected queue does not support timestamps");
        return 77;
    }
    require(status, "pool");
    printf("Timestamp adapter: %s software=%u bits=%u period_ns=%g; not hardware qualification or performance evidence\n",
        caps.device_name, caps.software_adapter, caps.timestamp_valid_bits, caps.timestamp_period_ns);
    doe_vk_buffer *readback = NULL;
    require(doe_vk_buffer_create(context, 48, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &readback, &error), "result");
    uint64_t initial[4] = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    require(doe_vk_buffer_write(readback, 0, initial, sizeof(initial)), "sentinels");
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    require(doe_vk_commands_reset_queries(commands, pool, 0, 2), "reset");
    require(doe_vk_commands_reset_queries(commands, pool, 3, 1), "reset unwritten");
    require(doe_vk_commands_write_timestamp(commands, pool, 0, DOE_VK_TIMESTAMP_BEGIN), "first timestamp");
    require(doe_vk_commands_fill(commands, readback, 32, 16, UINT32_C(0x12345678)), "fill");
    require(doe_vk_commands_write_timestamp(commands, pool, 1, DOE_VK_TIMESTAMP_END), "second timestamp");
    require(doe_vk_commands_resolve_queries(commands, pool, 0, 4, readback, 0), "resolve");
    doe_vk_query_pool_release(pool);
    require(doe_vk_commands_submit(commands, &error), "submit");
    if (doe_vk_commands_release(commands) != DOE_VK_BUSY) return 1;
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait");
    uint64_t ticks[4];
    uint32_t payload[4];
    require(doe_vk_buffer_read(readback, 0, ticks, sizeof(ticks)), "ticks");
    require(doe_vk_buffer_read(readback, 32, payload, sizeof(payload)), "payload");
    if (ticks[2] != 0 || ticks[3] != 0) return 1;
    for (unsigned i = 0; i < 4; ++i) if (payload[i] != UINT32_C(0x12345678)) return 1;
    uint64_t mask = caps.timestamp_valid_bits == 64 ? UINT64_MAX : (UINT64_C(1) << caps.timestamp_valid_bits) - 1;
    printf("Observed native ticks: %llu,%llu; wrap-aware delta=%llu (no benchmark claim)\n",
        (unsigned long long)(ticks[0] & mask), (unsigned long long)(ticks[1] & mask),
        (unsigned long long)((ticks[1] - ticks[0]) & mask));
    require(doe_vk_commands_release(commands), "retire");
    doe_vk_buffer_release(readback);
    doe_vk_context_release(context);
    puts("PASS real timestamp reset/write/resolve with zero for never-written/reset entries and payload visibility, pending-release refusal and early pool caller release");
    return 0;
}
