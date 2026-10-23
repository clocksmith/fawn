/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* Public error categorization is ABI conversion, not admission. Keep native
 * allocation/execution failure distinct from rejected application arguments. */
WGPUErrorType native_resource_error(doe_resource_status status)
{
    switch (status) {
    case DOE_RESOURCE_OK: return WGPUErrorType_NoError;
    case DOE_RESOURCE_OUT_OF_MEMORY: return WGPUErrorType_OutOfMemory;
    case DOE_RESOURCE_PENDING:
    case DOE_RESOURCE_EXECUTION_FAILED: return WGPUErrorType_Internal;
    default: return WGPUErrorType_Validation;
    }
}

WGPUErrorType native_backend_error(doe_vk_status status)
{
    switch (status) {
    case DOE_VK_OK: return WGPUErrorType_NoError;
    case DOE_VK_OUT_OF_MEMORY: return WGPUErrorType_OutOfMemory;
    case DOE_VK_INVALID_ARGUMENT:
    case DOE_VK_UNSUPPORTED:
    case DOE_VK_INVALID_STATE:
    case DOE_VK_BUSY: return WGPUErrorType_Validation;
    default: return WGPUErrorType_Internal;
    }
}

WGPUErrorType native_label_error(doe_label_status status)
{
    switch (status) {
    case DOE_LABEL_OK: return WGPUErrorType_NoError;
    case DOE_LABEL_OUT_OF_MEMORY: return WGPUErrorType_OutOfMemory;
    case DOE_LABEL_INVALID_ARGUMENT:
    case DOE_LABEL_UNSUPPORTED_VERSION:
    case DOE_LABEL_LAYOUT_OVERFLOW: return WGPUErrorType_Validation;
    default: return WGPUErrorType_Internal;
    }
}
