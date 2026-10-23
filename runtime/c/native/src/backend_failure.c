/* SPDX-License-Identifier: MIT */
#include "backend.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void copy_text(char *destination, size_t capacity, const char *source)
{
    size_t size = 0;
    if (source) while (size + 1 < capacity && source[size]) ++size;
    if (size) memcpy(destination, source, size);
    destination[size] = '\0';
}

native_failure native_failure_status(native_result status)
{
    return (native_failure){.status = status};
}

native_failure native_failure_vulkan(doe_vk_status status, const doe_vk_error *error)
{
    native_failure failure = {.status = native_from_vulkan(status), .backend = WGPUBackendType_Vulkan};
    if (status != DOE_VK_OK && error && error->status == status) {
        failure.native_code = error->native_result;
        copy_text(failure.operation, sizeof(failure.operation), error->operation);
    }
    return failure;
}

native_failure native_failure_metal(doe_mtl_status status, const doe_mtl_error *error)
{
    native_failure failure = {.status = native_from_metal(status), .backend = WGPUBackendType_Metal};
    if (status != DOE_MTL_OK && error && error->status == status) {
        failure.native_code = error->native_code;
        copy_text(failure.operation, sizeof(failure.operation), error->operation);
        copy_text(failure.message, sizeof(failure.message), error->message);
    }
    return failure;
}

void native_failure_format(const native_failure *failure, const char *fallback,
    char output[NATIVE_FAILURE_MESSAGE_CAPACITY])
{
    int previous_errno = errno;
    if (failure->status == NATIVE_OK) output[0] = '\0';
    else if (!failure->operation[0]) copy_text(output, NATIVE_FAILURE_MESSAGE_CAPACITY, fallback);
    else {
        const char *backend = failure->backend == WGPUBackendType_Metal ? "Metal" : "Vulkan";
        (void)snprintf(output, NATIVE_FAILURE_MESSAGE_CAPACITY, "%s: %s (native=%" PRId64 ")%s%s",
            backend, failure->operation, failure->native_code,
            failure->message[0] ? ": " : "", failure->message);
    }
    errno = previous_errno;
}
