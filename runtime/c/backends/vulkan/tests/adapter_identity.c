/* SPDX-License-Identifier: MIT */
#include "adapter_identity_fixture.h"

static doe_vk_caps select(const doe_vk_options *options, uint32_t expected, uint32_t ordinal) {
    doe_vk_context selected = context();
    doe_vk_error error;
    assert(doe_vk_choose_device(&selected, options, &error) == DOE_VK_OK);
    assert(selected.physical == handle(expected) && selected.caps.adapter_ordinal == ordinal);
    assert(selected.caps.software_adapter == (expected == 2));
    return selected.caps;
}
static void reject(const doe_vk_options *options, doe_vk_status status, const char *operation) {
    doe_vk_context selected = context();
    doe_vk_error error;
    assert(doe_vk_choose_device(&selected, options, &error) == status);
    assert(error.status == status && strcmp(error.operation, operation) == 0);
    assert(selected.physical == NULL);
    const doe_vk_caps empty = {0};
    assert(memcmp(&selected.caps, &empty, sizeof(empty)) == 0);
}
static void queue_policies(void) {
    VkQueueFamilyProperties queues[4] = {
        {.queueFlags=VK_QUEUE_COMPUTE_BIT, .queueCount=0},
        {.queueFlags=VK_QUEUE_GRAPHICS_BIT, .queueCount=1},
        {.queueFlags=VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT, .queueCount=1},
        {.queueFlags=VK_QUEUE_COMPUTE_BIT, .queueCount=1}
    };
    doe_vk_options options = {0};
    doe_vk_error error = {0};
    uint32_t selected = 99;
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_OK && selected == 3);
    options.queue_selection = DOE_VK_QUEUE_PREFER_GRAPHICS_COMPUTE;
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_OK && selected == 2);
    queues[2].queueCount = 0;
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_OK && selected == 3);
    queues[2].queueCount = 1;
    options.queue_selection = DOE_VK_QUEUE_REQUIRE_COMPUTE_ONLY;
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_OK && selected == 3);
    selected = 99;
    assert(doe_vk_select_queue(queues, 3, &options, &selected, &error) == DOE_VK_UNSUPPORTED && selected == 99);
    assert(strstr(error.operation, "compute-only"));
    options.require_graphics_queue = true;
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_INVALID_ARGUMENT && selected == 99);
    options.require_graphics_queue = false;
    options.surface_platforms = DOE_VK_SURFACE_XCB;
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_INVALID_ARGUMENT && selected == 99);
    options.surface_platforms = 0;
    options.queue_selection = (doe_vk_queue_selection)99;
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_INVALID_ARGUMENT && selected == 99);
    options = (doe_vk_options){.require_graphics_queue=true};
    assert(doe_vk_select_queue(queues, 4, &options, &selected, &error) == DOE_VK_OK && selected == 2);
}

int main(void) {
    queue_policies();
    doe_vk_options options = {0};
    doe_vk_caps original = select(&options, 0, 0);
    generation = 1;
    order[0] = 1;
    order[1] = 0;
    select(&options, 1, 0); /* Wrong-but-valid ordinal control changes device. */
    options.adapter_selection = DOE_VK_SELECT_IDENTITY;
    options.required_identity = original.identity;
    doe_vk_caps same = select(&options, 0, 1);
    assert(memcmp(&same.identity, &original.identity, sizeof(same.identity)) == 0);
    ++options.required_identity.driver_uuid[0];
    reject(&options, DOE_VK_UNSUPPORTED, "selected adapter unavailable");
    options.required_identity = original.identity;
    duplicate = true;
    reject(&options, DOE_VK_UNSUPPORTED, "ambiguous adapter identity");
    duplicate = false;
    options.required_identity.device_uuid[0] = 0;
    reject(&options, DOE_VK_UNSUPPORTED, "selected adapter unavailable");

    options = (doe_vk_options){.adapter_ordinal=UINT32_MAX,
        .allow_cpu_adapter=true,.adapter_selection=DOE_VK_SELECT_SOFTWARE};
    doe_vk_caps software = select(&options, 2, 2);
    order[0] = 2;
    order[1] = 0;
    order[2] = 1;
    generation = 0;
    options.adapter_selection = DOE_VK_SELECT_IDENTITY;
    options.required_identity = software.identity;
    select(&options, 2, 0);
    order[0] = 1;
    available = 2;
    reject(&options, DOE_VK_UNSUPPORTED, "selected adapter unavailable");
    options.adapter_selection = DOE_VK_SELECT_SOFTWARE;
    reject(&options, DOE_VK_UNSUPPORTED, "selected adapter unavailable");
    options.allow_cpu_adapter = false;
    unsigned before = enumerations;
    reject(&options, DOE_VK_INVALID_ARGUMENT, "software selection requires CPU permission");
    assert(enumerations == before);
    options.adapter_selection = (doe_vk_adapter_selection)99;
    reject(&options, DOE_VK_INVALID_ARGUMENT, "unknown adapter selection");
    assert(enumerations == before);
    /* HARDWARE_ELSE_SOFTWARE: the first non-CPU device, else the first CPU
     * device, from one enumeration; it needs CPU permission like SOFTWARE. */
    options = (doe_vk_options){.allow_cpu_adapter=true,.adapter_selection=DOE_VK_SELECT_HARDWARE_ELSE_SOFTWARE};
    available = 3;
    order[0] = 2;
    order[1] = 0;
    order[2] = 1;
    before = enumerations;
    select(&options, 0, 1);
    assert(enumerations == before + 2); /* One count and one list call. */
    available = 1;
    select(&options, 2, 0);
    options.allow_cpu_adapter = false;
    reject(&options, DOE_VK_INVALID_ARGUMENT, "software selection requires CPU permission");
    available = 3;
    order[0] = 0;
    order[1] = 1;
    order[2] = 2;

    options = (doe_vk_options){0};
    count_result = -1;
    reject(&options, DOE_VK_OUT_OF_MEMORY, "vkEnumeratePhysicalDevices");
    count_result = 0;
    list_result = 5;
    reject(&options, DOE_VK_NATIVE_FAILURE, "vkEnumeratePhysicalDevices");
    list_result = 0;
    oversized = true;
    reject(&options, DOE_VK_UNSUPPORTED, "adapter enumeration changed");
    oversized = false;
    device_version = 0;
    reject(&options, DOE_VK_UNSUPPORTED, "Vulkan 1.2 device required");
    device_version = VK_API_VERSION_1_2;
    available = 3;
    order[0] = 2;
    reject(&options, DOE_VK_UNSUPPORTED, "software adapter not allowed");
    options.adapter_ordinal = 3;
    reject(&options, DOE_VK_UNSUPPORTED, "adapter unavailable");
    puts("PASS UUID identity across handles/order, ambiguous/missing driver-device identity, explicit CPU selection, hardware-else-software selection and failed enumeration");
    return 0;
}
