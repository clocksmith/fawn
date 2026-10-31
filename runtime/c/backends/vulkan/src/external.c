/* SPDX-License-Identifier: MIT
 * OPAQUE_FD image import and SYNC_FD semaphore ownership transfer. One texture,
 * use, layout and initialization owner (texture_commands.c); the command owner
 * keeps every external semaphore until its recording is proven terminal. */
#include "texture_internal.h"
#include "surface_internal.h"
#include "../include/doe_vk_external.h"
#if defined(_WIN32)
/* POSIX descriptors only: Windows hosts report the capability unavailable. */
static int dup(int fd) { (void)fd; return -1; }
static int close(int fd) { (void)fd; return -1; }
#else
#include <unistd.h>
#endif

_Static_assert(DOE_VK_EXTERNAL_WAITS == sizeof(((doe_vk_commands *)0)->external_waits) /
    sizeof(VkSemaphore), "external wait capacity");

enum { VK_ERROR_FORMAT_NOT_SUPPORTED_RESULT = -11 };
static const VkFlags probe_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

/* UNSUPPORTED (no output) when the driver rejects the combination. */
static doe_vk_status import_properties(doe_vk_context *context, uint32_t format, VkFlags usage, VkFlags flags,
    VkImageFormatProperties *image, VkExternalMemoryProperties *memory, doe_vk_error *error) {
    const VkPhysicalDeviceExternalImageFormatInfo external = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
    const VkPhysicalDeviceImageFormatInfo2 info = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &external, .format = format, .type = VK_IMAGE_TYPE_2D, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage, .flags = flags};
    VkExternalImageFormatProperties external_properties = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 properties = {.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &external_properties};
    VkResult result = context->external.vkGetPhysicalDeviceImageFormatProperties2(context->physical, &info, &properties);
    if (result == VK_ERROR_FORMAT_NOT_SUPPORTED_RESULT)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "external image format", error);
    doe_vk_status status = doe_vk_result(context, result, "vkGetPhysicalDeviceImageFormatProperties2", error);
    if (status != DOE_VK_OK) return status;
    *image = properties.imageFormatProperties;
    *memory = external_properties.externalMemoryProperties;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_external_probe(doe_vk_context *context, bool *available, doe_vk_error *error) {
    *available = false;
#if defined(_WIN32)
    return DOE_VK_OK;
#endif
    /* Core 1.1 physical-device queries exported by the loader. A loader without
     * them cannot offer the capability; that is absence, not a context failure. */
    doe_vk_error ignored = {0};
    if (doe_vk_load_symbol(context->library, "vkGetPhysicalDeviceImageFormatProperties2",
            &context->external.vkGetPhysicalDeviceImageFormatProperties2,
            sizeof(context->external.vkGetPhysicalDeviceImageFormatProperties2), &ignored) != DOE_VK_OK ||
        doe_vk_load_symbol(context->library, "vkGetPhysicalDeviceExternalSemaphoreProperties",
            &context->external.vkGetPhysicalDeviceExternalSemaphoreProperties,
            sizeof(context->external.vkGetPhysicalDeviceExternalSemaphoreProperties), &ignored) != DOE_VK_OK) {
        context->external.vkGetPhysicalDeviceImageFormatProperties2 = NULL;
        context->external.vkGetPhysicalDeviceExternalSemaphoreProperties = NULL;
        return DOE_VK_OK;
    }
    const VkPhysicalDeviceExternalSemaphoreInfo semaphore = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
    VkExternalSemaphoreProperties semaphore_properties = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    context->external.vkGetPhysicalDeviceExternalSemaphoreProperties(context->physical, &semaphore, &semaphore_properties);
    const VkFlags both = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
    if ((semaphore_properties.externalSemaphoreFeatures & both) != both) return DOE_VK_OK;
    /* Producers such as Chromium's Vulkan compositor hand over OPAQUE_FD semaphores. */
    const VkPhysicalDeviceExternalSemaphoreInfo opaque = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
    VkExternalSemaphoreProperties opaque_properties = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    context->external.vkGetPhysicalDeviceExternalSemaphoreProperties(context->physical, &opaque, &opaque_properties);
    if (!(opaque_properties.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)) return DOE_VK_OK;
    VkImageFormatProperties image;
    VkExternalMemoryProperties memory;
    doe_vk_status status = import_properties(context, DOE_VK_FORMAT_BGRA8_UNORM, probe_usage, 0, &image, &memory, error);
    if (status == DOE_VK_UNSUPPORTED) return doe_vk_fail(DOE_VK_OK, "external image import unsupported", error);
    if (status != DOE_VK_OK) return status;
    *available = (memory.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
    return DOE_VK_OK;
}

static doe_vk_status device_proc(doe_vk_context *context, const char *name, void *destination, size_t size,
    doe_vk_error *error) {
    doe_vk_proc proc = context->vk.vkGetDeviceProcAddr(context->device, name);
    if (proc == NULL || size != sizeof(proc)) return doe_vk_fail(DOE_VK_UNSUPPORTED, name, error);
    memcpy(destination, &proc, size);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_external_load_device(doe_vk_context *context, doe_vk_error *error) {
    doe_vk_status status = DOE_VK_OK;
    if (context->vk.vkGetDeviceProcAddr == NULL)
        status = doe_vk_load_symbol(context->library, "vkGetDeviceProcAddr", &context->vk.vkGetDeviceProcAddr,
            sizeof(context->vk.vkGetDeviceProcAddr), error);
    if (status == DOE_VK_OK && context->vk.vkCreateSemaphore == NULL)
        status = doe_vk_load_symbol(context->library, "vkCreateSemaphore", &context->vk.vkCreateSemaphore,
            sizeof(context->vk.vkCreateSemaphore), error);
    if (status == DOE_VK_OK && context->vk.vkDestroySemaphore == NULL)
        status = doe_vk_load_symbol(context->library, "vkDestroySemaphore", &context->vk.vkDestroySemaphore,
            sizeof(context->vk.vkDestroySemaphore), error);
    if (status == DOE_VK_OK)
        status = device_proc(context, "vkImportSemaphoreFdKHR", &context->external.vkImportSemaphoreFdKHR,
            sizeof(context->external.vkImportSemaphoreFdKHR), error);
    if (status == DOE_VK_OK)
        status = device_proc(context, "vkGetSemaphoreFdKHR", &context->external.vkGetSemaphoreFdKHR,
            sizeof(context->external.vkGetSemaphoreFdKHR), error);
    return status;
}

void doe_vk_external_commands_release(doe_vk_commands *commands) {
    doe_vk_context *context = commands->context;
    for (uint32_t i = 0; i < commands->external_wait_count; ++i)
        context->vk.vkDestroySemaphore(context->device, commands->external_waits[i], NULL);
    commands->external_wait_count = 0;
    if (commands->external_signal) context->vk.vkDestroySemaphore(context->device, commands->external_signal, NULL);
    commands->external_signal = 0;
}

static bool admitted_format(doe_vk_texture_format format) {
    return format == DOE_VK_FORMAT_RGBA8_UNORM || format == DOE_VK_FORMAT_BGRA8_UNORM ||
        format == DOE_VK_FORMAT_RGBA16_FLOAT;
}

doe_vk_status doe_vk_external_image_desc_parse(const void *vk_image_create_info,
    doe_vk_external_image_desc *out) {
    const VkImageCreateInfo *info = vk_image_create_info;
    if (info == NULL || out == NULL || info->sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO)
        return DOE_VK_INVALID_ARGUMENT;
    const VkExternalMemoryImageCreateInfo *external = info->pNext;
    if (external == NULL || external->sType != VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO ||
        external->pNext != NULL || !(external->handleTypes & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT))
        return DOE_VK_UNSUPPORTED;
    if (info->imageType != VK_IMAGE_TYPE_2D || info->mipLevels != 1 || info->arrayLayers != 1 ||
        info->samples != 1 || info->tiling != VK_IMAGE_TILING_OPTIMAL ||
        info->sharingMode != VK_SHARING_MODE_EXCLUSIVE || info->extent.depth != 1 ||
        !admitted_format((doe_vk_texture_format)info->format))
        return DOE_VK_UNSUPPORTED;
    *out = (doe_vk_external_image_desc){.format = (doe_vk_texture_format)info->format,
        .width = info->extent.width, .height = info->extent.height, .flags = info->flags,
        .usage = info->usage, .handle_types = external->handleTypes, .memory_fd = -1};
    return DOE_VK_OK;
}

bool doe_vk_texture_imported(const doe_vk_texture *texture) {
    return texture != NULL && texture->imported;
}

static doe_vk_status validate_import(doe_vk_context *context, const doe_vk_external_image_desc *desc,
    doe_vk_format_caps *caps, doe_vk_error *error) {
    if (!context->caps.external_fd) return doe_vk_fail(DOE_VK_UNSUPPORTED, "external fd interop not enabled", error);
    if (desc->memory_fd < 0 || desc->width == 0 || desc->height == 0 || desc->allocation_size == 0 ||
        desc->memory_type_index >= context->memory.memoryTypeCount ||
        !(desc->handle_types & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT) ||
        (desc->flags & ~(uint32_t)VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) != 0)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "external image description", error);
    if (!admitted_format(desc->format)) return doe_vk_fail(DOE_VK_UNSUPPORTED, "external image format", error);
    doe_vk_status status = doe_vk_texture_format_caps(context, desc->format, caps);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "external image format caps", error);
    if (((desc->usage & DOE_VK_TEXTURE_SAMPLED) && !caps->sampled) ||
        ((desc->usage & DOE_VK_TEXTURE_STORAGE) && !caps->storage) ||
        ((desc->usage & DOE_VK_TEXTURE_COLOR_ATTACHMENT) && !caps->color_attachment) ||
        ((desc->usage & DOE_VK_TEXTURE_COPY_SRC) && !caps->copy_src) ||
        ((desc->usage & DOE_VK_TEXTURE_COPY_DST) && !caps->copy_dst) ||
        (desc->usage & DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT))
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "external image usage", error);
    VkImageFormatProperties image;
    VkExternalMemoryProperties memory;
    status = import_properties(context, desc->format, desc->usage, desc->flags, &image, &memory, error);
    if (status != DOE_VK_OK) return status;
    if (!(memory.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
        ((memory.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) && !desc->dedicated) ||
        desc->width > image.maxExtent.width || desc->height > image.maxExtent.height)
        return doe_vk_fail(DOE_VK_UNSUPPORTED, "external image import", error);
    return DOE_VK_OK;
}

doe_vk_status doe_vk_texture_import_opaque_fd(doe_vk_context *context,
    const doe_vk_external_image_desc *desc, doe_vk_texture **out, doe_vk_error *error) {
    if (context == NULL || desc == NULL || out == NULL)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "external image import", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "external image context", error);
    doe_vk_format_caps caps;
    doe_vk_status status = validate_import(context, desc, &caps, error);
    if (status != DOE_VK_OK) return status;
    doe_vk_texture *texture = calloc(1, sizeof(*texture));
    if (texture == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "external texture owner", error);
    status = doe_vk_context_retain(context);
    if (status != DOE_VK_OK) {
        free(texture);
        return doe_vk_fail(status, "external texture context retain", error);
    }
    texture->context = context;
    texture->references = 1;
    texture->imported = true;
    /* Owner-level usage is the WebGPU-visible subset; the native image keeps
     * the producer's exact flags below. */
    texture->desc = (doe_vk_texture_desc){.dimension = DOE_VK_TEXTURE_2D, .format = desc->format,
        .width = desc->width, .height = desc->height, .depth_or_layers = 1, .mip_levels = 1,
        .usage = desc->usage & UINT32_C(63), .sample_count = 1};
    texture->texel_bytes = caps.texel_bytes;
    if ((desc->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) && doe_vk_srgb_pair(desc->format) != 0) {
        texture->view_formats[0] = desc->format;
        texture->view_formats[1] = doe_vk_srgb_pair(desc->format);
        texture->view_format_count = 2;
    }
    texture->subresource_count = 1;
    texture->initialized = calloc(1, 1);
    if (texture->initialized == NULL) {
        status = doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "external texture state", error);
        goto failed;
    }
    const VkExternalMemoryImageCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = desc->handle_types};
    const VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &external,
        .flags = desc->flags, .imageType = VK_IMAGE_TYPE_2D, .format = (uint32_t)desc->format,
        .extent = {desc->width, desc->height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = 1,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = desc->usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    status = doe_vk_result(context, context->vk.vkCreateImage(context->device, &image, NULL, &texture->image),
        "vkCreateImage(external)", error);
    if (status != DOE_VK_OK) goto failed;
    VkMemoryRequirements requirements = {0};
    context->vk.vkGetImageMemoryRequirements(context->device, texture->image, &requirements);
    if (requirements.size == 0 || requirements.size > desc->allocation_size ||
        !(requirements.memoryTypeBits & (UINT32_C(1) << desc->memory_type_index))) {
        status = doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "external memory size or type", error);
        goto failed;
    }
    int fd = dup(desc->memory_fd);
    if (fd < 0) {
        status = doe_vk_fail(DOE_VK_NATIVE_FAILURE, "external memory descriptor duplicate", error);
        goto failed;
    }
    const VkMemoryDedicatedAllocateInfo dedicated = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = texture->image};
    const VkImportMemoryFdInfoKHR import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = desc->dedicated ? &dedicated : NULL, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        .fd = fd};
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
        .allocationSize = desc->allocation_size, .memoryTypeIndex = desc->memory_type_index};
    status = doe_vk_result(context, context->vk.vkAllocateMemory(context->device, &allocate, NULL, &texture->memory),
        "vkAllocateMemory(import opaque fd)", error);
    if (status != DOE_VK_OK) {
        (void)close(fd); /* Ownership transfers only on a successful import. */
        goto failed;
    }
    status = doe_vk_result(context, context->vk.vkBindImageMemory(context->device, texture->image, texture->memory, 0),
        "vkBindImageMemory(external)", error);
    if (status != DOE_VK_OK) goto failed;
    *out = texture;
    return doe_vk_fail(DOE_VK_OK, "external texture imported", error);
failed:
    doe_vk_texture_release(texture);
    return status;
}

static bool external_layout(uint32_t layout) {
    return layout == VK_IMAGE_LAYOUT_UNDEFINED || layout == VK_IMAGE_LAYOUT_GENERAL ||
        layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL || layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

static void ownership_barrier(doe_vk_commands *commands, const doe_vk_texture *texture, bool acquire,
    uint32_t old_layout, uint32_t new_layout) {
    const uint32_t family = commands->context->caps.queue_family;
    const VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = acquire ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = acquire ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0,
        .oldLayout = old_layout, .newLayout = new_layout,
        .srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_EXTERNAL : family,
        .dstQueueFamilyIndex = acquire ? family : VK_QUEUE_FAMILY_EXTERNAL,
        .image = texture->image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    commands->context->vk.vkCmdPipelineBarrier(commands->command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static doe_vk_status import_wait(doe_vk_commands *commands, doe_vk_external_wait wait) {
    doe_vk_context *context = commands->context;
    const VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore semaphore = 0;
    if (context->vk.vkCreateSemaphore(context->device, &create, NULL, &semaphore) != 0) return DOE_VK_OUT_OF_MEMORY;
    int fd = dup(wait.fd);
    const VkImportSemaphoreFdInfoKHR import = {.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = semaphore, .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
        .handleType = wait.opaque ? VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT :
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, .fd = fd};
    if (fd < 0 || context->external.vkImportSemaphoreFdKHR(context->device, &import) != 0) {
        if (fd >= 0) (void)close(fd); /* The import consumes the descriptor only on success. */
        context->vk.vkDestroySemaphore(context->device, semaphore, NULL);
        return DOE_VK_NATIVE_FAILURE;
    }
    commands->external_waits[commands->external_wait_count++] = semaphore;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_external_acquire(doe_vk_commands *commands, doe_vk_texture *texture,
    const doe_vk_external_wait *waits, uint32_t wait_count, uint32_t old_layout, uint32_t new_layout,
    bool initialized) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (texture == NULL || texture->context != commands->context || !texture->imported ||
        texture->external_acquired || (wait_count != 0 && waits == NULL) ||
        !external_layout(old_layout) || !external_layout(new_layout))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    uint32_t imports = 0;
    for (uint32_t i = 0; i < wait_count; ++i) {
        /* An opaque semaphore has no "already signaled" descriptor value. */
        if (waits[i].opaque && waits[i].fd < 0) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
        imports += waits[i].fd >= 0;
    }
    if (imports > DOE_VK_EXTERNAL_WAITS - commands->external_wait_count)
        return doe_vk_poison(commands, DOE_VK_UNSUPPORTED);
    for (uint32_t i = 0; i < wait_count; ++i) {
        if (waits[i].fd < 0) continue; /* A signaled sync file needs no wait. */
        status = import_wait(commands, waits[i]);
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    }
    texture->external_acquired = true;
    doe_vk_texture_use *use = NULL;
    status = doe_vk_use_texture(commands, texture, &use);
    if (status != DOE_VK_OK) {
        texture->external_acquired = false;
        return doe_vk_poison(commands, status);
    }
    /* The acquire repeats the producer's release layouts. A transition to
     * UNDEFINED is invalid, so that pair discards into GENERAL instead. */
    const uint32_t target = new_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_GENERAL : new_layout;
    ownership_barrier(commands, texture, true, old_layout, target);
    use->layout = target;
    const bool kept = initialized && new_layout != VK_IMAGE_LAYOUT_UNDEFINED && old_layout != VK_IMAGE_LAYOUT_UNDEFINED;
    use->initialized[0] = kept ? (uint8_t)VK_IMAGE_ASPECT_COLOR_BIT : 0;
    return kept ? DOE_VK_OK : doe_vk_commands_initialize_texture(commands, texture);
}

doe_vk_status doe_vk_commands_external_release(doe_vk_commands *commands, doe_vk_texture *texture,
    uint32_t *layout, bool *initialized) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (texture == NULL || layout == NULL || initialized == NULL || texture->context != commands->context ||
        !texture->imported || !texture->external_acquired || commands->external_signal != 0)
        return doe_vk_poison(commands, DOE_VK_INVALID_STATE);
    doe_vk_texture_use *use = NULL;
    status = doe_vk_use_texture(commands, texture, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_context *context = commands->context;
    const VkExportSemaphoreCreateInfo exportable = {.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
    const VkSemaphoreCreateInfo create = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &exportable};
    VkSemaphore semaphore = 0;
    status = doe_vk_result(context, context->vk.vkCreateSemaphore(context->device, &create, NULL, &semaphore),
        "vkCreateSemaphore(export sync fd)", NULL);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    const uint32_t old_layout = use->layout;
    const uint32_t released = old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_GENERAL : old_layout;
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) use->initialized[0] = 0;
    ownership_barrier(commands, texture, false, old_layout, released);
    use->layout = released;
    commands->external_signal = semaphore;
    texture->external_acquired = false;
    *layout = released;
    *initialized = (use->initialized[0] & VK_IMAGE_ASPECT_COLOR_BIT) != 0;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_commands_export_sync_fd(doe_vk_commands *commands, int *fd, doe_vk_error *error) {
    if (commands == NULL || fd == NULL || commands->external_signal == 0 || !commands->committed ||
        (commands->state != DOE_VK_SUBMITTED && commands->state != DOE_VK_COMPLETE))
        return doe_vk_fail(DOE_VK_INVALID_STATE, "export sync fd before accepted release", error);
    doe_vk_context *context = commands->context;
    const VkSemaphoreGetFdInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = commands->external_signal, .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
    int exported = -1;
    doe_vk_status status = doe_vk_result(context, context->external.vkGetSemaphoreFdKHR(context->device, &info, &exported),
        "vkGetSemaphoreFdKHR(sync fd)", error);
    if (status != DOE_VK_OK) return status;
    *fd = exported;
    return DOE_VK_OK;
}

int doe_vk_external_duplicate_fd(int fd) {
    return fd >= 0 ? dup(fd) : -1;
}

void doe_vk_external_close_fd(int fd) {
    if (fd >= 0) (void)close(fd);
}
