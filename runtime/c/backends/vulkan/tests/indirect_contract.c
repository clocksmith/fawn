/* SPDX-License-Identifier: MIT */
#include "../src/indirect_internal.h"
#include <assert.h>
#include <stdio.h>

int main(int argc, char **argv) {
    uint32_t limits[3] = {2, 3, 4};
    char source[DOE_VK_INDIRECT_SOURCE_CAPACITY] = {0};
    size_t length = SIZE_MAX;
    assert(doe_vk_dispatch_validator_source(limits, source, sizeof(source), &length) == DOE_VK_OK);
    assert(length == strlen(source) && strstr(source, "@workgroup_size(1)") != NULL);
    assert(strstr(source, "counts[0] > 2u || counts[1] > 3u || counts[2] > 4u") != NULL);
    assert(strstr(source, "counts[0] = 0u;") != NULL && strstr(source, "counts[2] = 0u;") != NULL);
    if (argc == 2) {
        FILE *file = fopen(argv[1], "wb");
        assert(file != NULL && fwrite(source, 1, length, file) == length && fclose(file) == 0);
    } else assert(argc == 1);
    char before[sizeof(source)];
    memcpy(before, source, sizeof(source));
    size_t before_length = length;
    assert(doe_vk_dispatch_validator_source(NULL, source, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_dispatch_validator_source(limits, source, length, &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_dispatch_validator_source(limits, NULL, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_dispatch_validator_source(limits, source, sizeof(source), NULL) == DOE_VK_INVALID_ARGUMENT);
    limits[2] = 0;
    assert(doe_vk_dispatch_validator_source(limits, source, sizeof(source), &length) == DOE_VK_INVALID_ARGUMENT);
    assert(memcmp(before, source, sizeof(source)) == 0 && length == before_length);
    limits[0] = limits[1] = limits[2] = UINT32_MAX;
    assert(doe_vk_dispatch_validator_source(limits, source, sizeof(source), &length) == DOE_VK_OK);
    assert(strstr(source, "counts[2] > 4294967295u") != NULL);
    doe_vk_caps caps = {.max_workgroup_count = {2, 3, 4}, .max_workgroup_size = {1, 1, 1},
        .max_workgroup_invocations = 1, .max_storage_buffer_range = 12, .max_bind_groups = 1,
        .max_bindings_per_group = 1, .max_storage_buffers_per_stage = 1};
    assert(doe_vk_dispatch_validator_validate(&caps, limits) == DOE_VK_UNSUPPORTED);
    limits[0] = 2;
    limits[1] = 3;
    limits[2] = 4;
    assert(doe_vk_dispatch_validator_validate(&caps, limits) == DOE_VK_OK);
    caps.max_storage_buffer_range = 11;
    assert(doe_vk_dispatch_validator_validate(&caps, limits) == DOE_VK_UNSUPPORTED);
    caps.max_storage_buffer_range = 12;
    caps.max_workgroup_count[2] = 3;
    assert(doe_vk_dispatch_validator_validate(&caps, limits) == DOE_VK_UNSUPPORTED);
    caps.max_workgroup_count[2] = 4;
    caps.max_workgroup_size[1] = 0;
    assert(doe_vk_dispatch_validator_validate(&caps, limits) == DOE_VK_UNSUPPORTED);
    caps.max_workgroup_size[1] = 1;
    caps.max_storage_buffers_per_stage = 0;
    assert(doe_vk_dispatch_validator_validate(&caps, limits) == DOE_VK_UNSUPPORTED);
    caps.max_storage_buffers_per_stage = 1;
    assert(doe_vk_dispatch_validator_validate(NULL, limits) == DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_dispatch_validator_validate(&caps, NULL) == DOE_VK_INVALID_ARGUMENT);
    doe_vk_context context = {.references = 1, .caps = caps};
    doe_vk_dispatch_validator *sentinel = (doe_vk_dispatch_validator *)&context, *out = sentinel;
    doe_vk_dispatch_validator_desc desc = {.max_workgroup_count = {2, 3, 4}};
    assert(doe_vk_dispatch_validator_create(&context, &desc, &out, NULL) == DOE_VK_INVALID_ARGUMENT);
    context.lost = true;
    assert(doe_vk_dispatch_validator_create(&context, &desc, &out, NULL) == DOE_VK_DEVICE_LOST);
    assert(out == sentinel && context.references == 1);
    doe_vk_dispatch_validator fake = {.references = UINT32_MAX};
    assert(doe_vk_dispatch_validator_retain(&fake) == DOE_VK_OUT_OF_MEMORY && fake.references == UINT32_MAX);
    puts("PASS indirect WGSL source constants, exact capacity/output preservation, device/requested-limit admission, invalid shader construction, lost context and retain overflow");
    return 0;
}
