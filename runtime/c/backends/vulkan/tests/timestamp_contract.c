#include "../src/internal.h"
#include "../include/doe_vk_timestamp.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    char source[DOE_VK_TIMESTAMP_SOURCE_CAPACITY];
    size_t length = SIZE_MAX;
    assert(doe_vk_timestamp_shader_source(2.5f, 48, 64, source, sizeof(source), &length) == DOE_VK_OK);
    const char prefix[] = "const MANTISSA = 10485760u;\nconst PERIOD_SHIFT = -22i;\n"
        "const MASK_LOW = 4294967295u;\nconst MASK_HIGH = 65535u;\nconst WORKGROUP_SIZE = 64u;\n";
    assert(strncmp(source, prefix, sizeof(prefix) - 1) == 0);
    assert(length == strlen(source) && length < sizeof(source));
    assert(strstr(source, "arrayLength(&timestamps)") != NULL);
    uint32_t bits = 1;
    float subnormal;
    memcpy(&subnormal, &bits, sizeof(subnormal));
    assert(doe_vk_timestamp_shader_source(subnormal, 64, 1, source, sizeof(source), &length) == DOE_VK_OK);
    assert(strncmp(source, "const MANTISSA = 1u;\nconst PERIOD_SHIFT = -149i;\n", 45) == 0);
    assert(doe_vk_timestamp_shader_source(FLT_MAX, 1, UINT32_MAX, source, sizeof(source), &length) == DOE_VK_OK);
    assert(strstr(source, "const MASK_LOW = 1u;\nconst MASK_HIGH = 0u;\n") != NULL);
    assert(strstr(source, "const PERIOD_SHIFT = 104i;") != NULL);
    char untouched[DOE_VK_TIMESTAMP_SOURCE_CAPACITY];
    memcpy(untouched, source, sizeof(source));
    size_t before = length;
    const float invalid[] = {0, -1, INFINITY, NAN};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(doe_vk_timestamp_shader_source(invalid[i], 64, 64, source, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_shader_source(1, 0, 64, source, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_shader_source(1, 65, 64, source, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_shader_source(1, 64, 0, source, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_shader_source(1, 64, 64, source, 1, &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_shader_source(1, 64, 64, NULL, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_shader_source(1, 64, 64, source, sizeof(source), NULL) == DOE_VK_INVALID_ARGUMENT);
    assert(length == before && memcmp(source, untouched, sizeof(source)) == 0);
    doe_vk_context context = {.references = 1};
    doe_vk_timestamp_normalizer *normalizer = NULL;
    doe_vk_timestamp_normalizer_desc descriptor = {.capacity = 1, .workgroup_size = 64};
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_UNSUPPORTED);
    descriptor.capacity = 0;
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_INVALID_ARGUMENT);
    descriptor.capacity = 1;
    context.lost = true;
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_DEVICE_LOST);
    assert(normalizer == NULL);
    context.lost = false;
    context.caps = (doe_vk_caps){.timestamp_valid_bits = 64, .timestamp_period_ns = 1,
        .max_workgroup_size = {64, 1, 1}, .max_workgroup_invocations = 64,
        .max_workgroup_count = {1, 1, 1}, .max_storage_buffer_range = 512};
    assert(doe_vk_timestamp_normalizer_validate(NULL, 1, 64) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_normalizer_validate(&context.caps, 0, 64) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_normalizer_validate(&context.caps, 1, 0) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_timestamp_normalizer_validate(&context.caps, 64, 64) == DOE_VK_OK);
    assert(doe_vk_timestamp_normalizer_validate(&context.caps, 65, 64) == DOE_VK_UNSUPPORTED);
    assert(doe_vk_timestamp_normalizer_validate(&context.caps, 1, 1) == DOE_VK_OK);
    assert(doe_vk_timestamp_normalizer_validate(&context.caps, 2, 1) == DOE_VK_UNSUPPORTED);
    assert(doe_vk_timestamp_normalizer_validate(&context.caps, UINT32_MAX, 64) == DOE_VK_UNSUPPORTED);
    descriptor.capacity = 65;
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_UNSUPPORTED);
    descriptor.capacity = 1;
    descriptor.workgroup_size = 65;
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_UNSUPPORTED);
    descriptor.workgroup_size = 64;
    descriptor.period_ns = 2;
    descriptor.valid_bits = 64;
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_INVALID_ARGUMENT);
    descriptor.period_ns = 1;
    descriptor.valid_bits = 48;
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_INVALID_ARGUMENT);
    descriptor.valid_bits = 64;
    assert(doe_vk_timestamp_normalizer_create(&context, &descriptor, &normalizer, NULL) == DOE_VK_INVALID_ARGUMENT);
    assert(normalizer == NULL && context.references == 1);
    doe_vk_commands commands = {.context = &context, .state = DOE_VK_RECORDING};
    context.lost = false;
    assert(doe_vk_commands_resolve_queries_ns(&commands, NULL, 0, 0, NULL, NULL, 0) == DOE_VK_INVALID_ARGUMENT);
    assert(commands.state == DOE_VK_POISONED);
    puts("PASS binary32 calibration, exact counter masks, subnormal/maximum periods, unchanged failure outputs and normalizer limit rejection");
    return 0;
}
