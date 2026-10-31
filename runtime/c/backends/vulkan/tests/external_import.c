/* SPDX-License-Identifier: MIT
 * Two Vulkan devices on one physical adapter: a raw producer device exports an
 * OPAQUE_FD image and SYNC_FD semaphores the way Chromium's ExternalVkImage
 * backing does; the Doe owner imports, acquires, reads, writes and releases it.
 * Pixels are checked on both sides: the producer's GPU clear through Doe, and
 * Doe's GPU upload back through the producer after waiting Doe's sync file.
 * Prints the selected adapter. Exit 77 when the interop is unavailable. */
#include "../src/texture_internal.h"
#include "../include/doe_vk_external.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXPORT_MEMORY_ALLOCATE_INFO 1000072002
#define MEMORY_GET_FD_INFO_KHR 1000074002
enum { SIZE = 16, BYTES = SIZE * SIZE * 4 };
typedef struct { VkStructureType sType; const void *pNext; VkFlags handleTypes; } ExportMemoryAllocateInfo;
typedef struct { VkStructureType sType; const void *pNext; VkDeviceMemory memory; uint32_t handleType; } MemoryGetFdInfo;
typedef union { float f[4]; uint32_t u[4]; } ClearColor;

typedef struct producer {
    doe_vk_context *context; /* Borrowed: instance, physical device and loader table. */
    VkDevice device;
    VkQueue queue;
    VkCommandPool pool;
    VkSemaphore held_signal;
    VkCommandBuffer held_command;
    VkResult (*get_memory_fd)(VkDevice, const MemoryGetFdInfo *, int *);
    VkResult (*get_semaphore_fd)(VkDevice, const VkSemaphoreGetFdInfoKHR *, int *);
    VkResult (*import_semaphore_fd)(VkDevice, const VkImportSemaphoreFdInfoKHR *);
    void (*clear)(VkCommandBuffer, VkImage, uint32_t, const ClearColor *, uint32_t, const VkImageSubresourceRange *);
} producer;

static void check(int ok, const char *what) {
    if (ok) return;
    fprintf(stderr, "FAIL %s\n", what);
    exit(1);
}
static void require(doe_vk_status status, const char *what) {
    if (status == DOE_VK_OK) return;
    fprintf(stderr, "FAIL %s: %s\n", what, doe_vk_status_name(status));
    exit(1);
}
static void proc(producer *p, const char *name, void *out) {
    doe_vk_proc f = p->context->vk.vkGetDeviceProcAddr(p->device, name);
    check(f != NULL, name);
    memcpy(out, &f, sizeof(f));
}
static uint32_t memory_type(const doe_vk_context *context, uint32_t bits, uint32_t flags) {
    for (uint32_t i = 0; i < context->memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (context->memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    check(0, "memory type");
    return 0;
}
static void producer_open(producer *p, doe_vk_context *context) {
    p->context = context;
    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = context->caps.queue_family, .queueCount = 1, .pQueuePriorities = &priority};
    const uint8_t *extensions[] = {(const uint8_t *)"VK_KHR_external_memory_fd",
        (const uint8_t *)"VK_KHR_external_semaphore_fd"};
    const VkDeviceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    check(context->vk.vkCreateDevice(context->physical, &create, NULL, &p->device) == 0, "producer device");
    context->vk.vkGetDeviceQueue(p->device, context->caps.queue_family, 0, &p->queue);
    const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = context->caps.queue_family};
    check(context->vk.vkCreateCommandPool(p->device, &pool, NULL, &p->pool) == 0, "producer pool");
    proc(p, "vkGetMemoryFdKHR", &p->get_memory_fd);
    proc(p, "vkGetSemaphoreFdKHR", &p->get_semaphore_fd);
    proc(p, "vkImportSemaphoreFdKHR", &p->import_semaphore_fd);
    proc(p, "vkCmdClearColorImage", &p->clear);
}
static VkCommandBuffer begin(producer *p) {
    const VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = p->pool, .commandBufferCount = 1};
    VkCommandBuffer command = NULL;
    check(p->context->vk.vkAllocateCommandBuffers(p->device, &allocate, &command) == 0, "producer command");
    const VkCommandBufferBeginInfo info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(p->context->vk.vkBeginCommandBuffer(command, &info) == 0, "producer begin");
    return command;
}
static void barrier(producer *p, VkCommandBuffer command, VkImage image, uint32_t old_layout, uint32_t new_layout,
    uint32_t source_family, uint32_t destination_family) {
    const VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = old_layout, .newLayout = new_layout, .srcQueueFamilyIndex = source_family,
        .dstQueueFamilyIndex = destination_family, .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    p->context->vk.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}
/* Submits with an optional imported sync-file wait and an optional exported
 * signal (a sync file, or an OPAQUE_FD semaphore as Chromium's pool exports); waits idle. */
static int submit(producer *p, VkCommandBuffer command, int wait_fd, bool export_signal, bool opaque) {
    check(p->context->vk.vkEndCommandBuffer(command) == 0, "producer end");
    VkSemaphore wait = 0, signal = 0;
    const VkSemaphoreCreateInfo plain = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (wait_fd >= 0) {
        check(p->context->vk.vkCreateSemaphore(p->device, &plain, NULL, &wait) == 0, "producer wait semaphore");
        const VkImportSemaphoreFdInfoKHR import = {.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
            .semaphore = wait, .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, .fd = wait_fd};
        check(p->import_semaphore_fd(p->device, &import) == 0, "producer import sync fd");
    }
    if (export_signal) {
        const VkExportSemaphoreCreateInfo exportable = {.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .handleTypes = opaque ? VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT :
                VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
        const VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &exportable};
        check(p->context->vk.vkCreateSemaphore(p->device, &create, NULL, &signal) == 0, "producer signal semaphore");
    }
    const VkFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const VkSubmitInfo info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = wait ? 1 : 0,
        .pWaitSemaphores = wait ? &wait : NULL, .pWaitDstStageMask = wait ? &stage : NULL,
        .commandBufferCount = 1, .pCommandBuffers = &command, .signalSemaphoreCount = signal ? 1 : 0,
        .pSignalSemaphores = signal ? &signal : NULL};
    check(p->context->vk.vkQueueSubmit(p->queue, 1, &info, 0) == 0, "producer submit");
    int fd = -1;
    if (signal) {
        const VkSemaphoreGetFdInfoKHR get = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
            .semaphore = signal, .handleType = opaque ? VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT :
                VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
        check(p->get_semaphore_fd(p->device, &get, &fd) == 0, "producer export semaphore");
    }
    if (signal) {
        /* Hand off while the producer's work may still run: Doe must order on
         * the semaphore, not on producer idleness. Retired by the next submit. */
        p->held_signal = signal;
        p->held_command = command;
        return fd;
    }
    check(p->context->vk.vkQueueWaitIdle(p->queue) == 0, "producer idle");
    if (wait) p->context->vk.vkDestroySemaphore(p->device, wait, NULL);
    if (p->held_signal) p->context->vk.vkDestroySemaphore(p->device, p->held_signal, NULL);
    if (p->held_command) p->context->vk.vkFreeCommandBuffers(p->device, p->pool, 1, &p->held_command);
    p->held_signal = 0;
    p->held_command = NULL;
    p->context->vk.vkFreeCommandBuffers(p->device, p->pool, 1, &command);
    return fd;
}

int main(int argc, char **argv) {
    /* --opaque: the producer hands over an OPAQUE_FD semaphore (Chromium's pool). */
    const bool opaque_handoff = argc == 2 && strcmp(argv[1], "--opaque") == 0;
    doe_vk_options options = {.adapter_selection = DOE_VK_SELECT_HARDWARE, .require_graphics_queue = true,
        .enable_external_fd = true};
    doe_vk_context *context = NULL;
    doe_vk_error error = {0};
    doe_vk_status status = doe_vk_context_create(&options, &context, &error);
    if (status != DOE_VK_OK) {
        printf("SKIP external import: %s at %s\n", doe_vk_status_name(status), error.operation);
        return 77;
    }
    printf("External import adapter: %s; developer run, not qualification\n", context->caps.device_name);
    if (!context->caps.external_fd) {
        printf("SKIP external import: fd interop unavailable\n");
        doe_vk_context_release(context);
        return 77;
    }
    producer p = {0};
    producer_open(&p, context);

    /* Producer image as ExternalVkImageBacking creates it: exportable, dedicated. */
    const VkExternalMemoryImageCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
    const VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &external,
        .imageType = VK_IMAGE_TYPE_2D, .format = DOE_VK_FORMAT_BGRA8_UNORM, .extent = {SIZE, SIZE, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = 1, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage image = 0;
    check(context->vk.vkCreateImage(p.device, &image_info, NULL, &image) == 0, "producer image");
    VkMemoryRequirements requirements;
    context->vk.vkGetImageMemoryRequirements(p.device, image, &requirements);
    const VkMemoryDedicatedAllocateInfo dedicated = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = image};
    const ExportMemoryAllocateInfo exportable = {EXPORT_MEMORY_ALLOCATE_INFO, &dedicated,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
    const uint32_t type = memory_type(context, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &exportable,
        .allocationSize = requirements.size, .memoryTypeIndex = type};
    VkDeviceMemory memory = 0;
    check(context->vk.vkAllocateMemory(p.device, &allocate, NULL, &memory) == 0, "producer memory");
    check(context->vk.vkBindImageMemory(p.device, image, memory, 0) == 0, "producer bind");
    const MemoryGetFdInfo get_fd = {MEMORY_GET_FD_INFO_KHR, NULL, memory, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
    int memory_fd = -1;
    check(p.get_memory_fd(p.device, &get_fd, &memory_fd) == 0, "producer memory fd");

    /* Producer clears (0.25, 0.5, 0.75, 1) and releases in TRANSFER_DST layout. */
    VkCommandBuffer command = begin(&p);
    barrier(&p, command, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, UINT32_MAX, UINT32_MAX);
    const ClearColor color = {.f = {0.25f, 0.5f, 0.75f, 1.0f}};
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    p.clear(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    barrier(&p, command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        context->caps.queue_family, VK_QUEUE_FAMILY_EXTERNAL);
    int produced = submit(&p, command, -1, true, opaque_handoff);

    doe_vk_external_image_desc desc;
    require(doe_vk_external_image_desc_parse(&image_info, &desc), "parse create info");
    desc.memory_fd = memory_fd;
    desc.allocation_size = requirements.size;
    desc.memory_type_index = type;
    desc.dedicated = true;
    doe_vk_texture *texture = NULL;
    require(doe_vk_texture_import_opaque_fd(context, &desc, &texture, &error), "import");
    (void)close(memory_fd); /* The import holds its own duplicate. */
    check(doe_vk_texture_evict(texture) == DOE_VK_BUSY, "imported destroy is BUSY");

    /* Unacquired use fails closed. */
    doe_vk_buffer *readback = NULL, *upload = NULL;
    require(doe_vk_buffer_create(context, BYTES, DOE_VK_BUFFER_COPY_DST, DOE_VK_MEMORY_HOST, &readback, &error), "readback");
    require(doe_vk_buffer_create(context, BYTES, DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &upload, &error), "upload");
    const doe_vk_texture_region region = {.extent = {SIZE, SIZE, 1}};
    doe_vk_commands *commands = NULL;
    require(doe_vk_commands_create(context, &commands, &error), "commands");
    check(doe_vk_commands_read_texture(commands, texture, region, readback, (doe_vk_texture_buffer_layout){0}) ==
        DOE_VK_INVALID_STATE, "unacquired use rejected");
    require(doe_vk_commands_release(commands), "release poisoned");

    /* Acquire waits the producer's sync file on the queue, then reads. */
    require(doe_vk_commands_create(context, &commands, &error), "acquire commands");
    const doe_vk_external_wait produced_wait = {produced, opaque_handoff};
    require(doe_vk_commands_external_acquire(commands, texture, &produced_wait, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true), "acquire");
    (void)close(produced);
    require(doe_vk_commands_read_texture(commands, texture, region, readback, (doe_vk_texture_buffer_layout){0}), "read");
    unsigned char pixels[BYTES];
    for (size_t i = 0; i < BYTES; ++i) pixels[i] = (unsigned char)(i * 7 + 3);
    require(doe_vk_buffer_write(upload, 0, pixels, sizeof(pixels)), "upload bytes");
    require(doe_vk_commands_upload_texture(commands, upload, (doe_vk_texture_buffer_layout){0}, texture, region),
        "write");
    require(doe_vk_commands_submit(commands, &error), "submit acquire");
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait acquire");
    require(doe_vk_commands_release(commands), "release acquire");
    unsigned char seen[BYTES];
    require(doe_vk_buffer_read(readback, 0, seen, sizeof(seen)), "read bytes");
    for (size_t i = 0; i < BYTES; i += 4)
        check(seen[i] == 191 && seen[i + 1] == 128 && seen[i + 2] == 64 && seen[i + 3] == 255, "producer pixels in Doe");

    /* Release exports a sync file; the producer waits it and reads Doe's write. */
    require(doe_vk_commands_create(context, &commands, &error), "release commands");
    uint32_t layout = 0;
    bool initialized = false;
    require(doe_vk_commands_external_release(commands, texture, &layout, &initialized), "release");
    check(initialized && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, "released layout and contents");
    require(doe_vk_commands_submit(commands, &error), "submit release");
    int released = -1;
    require(doe_vk_commands_export_sync_fd(commands, &released, &error), "export");
    VkBuffer host = 0;
    VkDeviceMemory host_memory = 0;
    const VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = BYTES,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    check(context->vk.vkCreateBuffer(p.device, &buffer_info, NULL, &host) == 0, "producer buffer");
    VkMemoryRequirements buffer_requirements;
    context->vk.vkGetBufferMemoryRequirements(p.device, host, &buffer_requirements);
    const VkMemoryAllocateInfo host_allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_requirements.size, .memoryTypeIndex = memory_type(context,
            buffer_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    check(context->vk.vkAllocateMemory(p.device, &host_allocate, NULL, &host_memory) == 0, "producer host memory");
    check(context->vk.vkBindBufferMemory(p.device, host, host_memory, 0) == 0, "producer buffer bind");
    command = begin(&p);
    barrier(&p, command, image, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_EXTERNAL,
        context->caps.queue_family);
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {SIZE, SIZE, 1}};
    context->vk.vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host, 1, &copy);
    if (released >= 0) {
        (void)submit(&p, command, released, false, false); /* The import consumed `released`. */
    } else {
        (void)submit(&p, command, -1, false, false); /* -1: already signaled. */
    }
    void *mapped = NULL;
    check(context->vk.vkMapMemory(p.device, host_memory, 0, BYTES, 0, &mapped) == 0, "producer map");
    check(memcmp(mapped, pixels, BYTES) == 0, "Doe pixels in producer");
    context->vk.vkUnmapMemory(p.device, host_memory);

    /* Doe retires its release before tearing down; the producer frees its image. */
    require(doe_vk_commands_wait(commands, UINT64_C(5000000000), &error), "wait release");
    require(doe_vk_commands_release(commands), "release release");
    doe_vk_buffer_release(readback);
    doe_vk_buffer_release(upload);
    doe_vk_texture_release(texture);
    context->vk.vkDestroyBuffer(p.device, host, NULL);
    context->vk.vkFreeMemory(p.device, host_memory, NULL);
    context->vk.vkDestroyImage(p.device, image, NULL);
    context->vk.vkFreeMemory(p.device, memory, NULL);
    context->vk.vkDestroyCommandPool(p.device, p.pool, NULL);
    context->vk.vkDestroyDevice(p.device, NULL);
    doe_vk_context_release(context);
    printf("PASS external import (%s producer semaphore): producer clear read by Doe, Doe upload read by producer after its sync file\n",
        opaque_handoff ? "OPAQUE_FD" : "SYNC_FD");
    return 0;
}
