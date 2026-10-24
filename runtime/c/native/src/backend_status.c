/* SPDX-License-Identifier: MIT */
#include "backend.h"

native_result native_from_vulkan(doe_vk_status status)
{
    switch (status) {
    case DOE_VK_OK: return NATIVE_OK;
    case DOE_VK_PENDING: return NATIVE_PENDING;
    case DOE_VK_TIMEOUT: return NATIVE_TIMEOUT;
    case DOE_VK_INVALID_ARGUMENT: return NATIVE_INVALID_ARGUMENT;
    case DOE_VK_OUT_OF_MEMORY: return NATIVE_OUT_OF_MEMORY;
    case DOE_VK_LOADER_UNAVAILABLE: return NATIVE_UNAVAILABLE;
    case DOE_VK_UNSUPPORTED: return NATIVE_UNSUPPORTED;
    case DOE_VK_NATIVE_FAILURE: return NATIVE_FAILURE;
    case DOE_VK_DEVICE_LOST: return NATIVE_DEVICE_LOST;
    case DOE_VK_BUSY: return NATIVE_BUSY;
    case DOE_VK_INVALID_STATE: return NATIVE_INVALID_STATE;
    case DOE_VK_SURFACE_OUTDATED: return NATIVE_SURFACE_OUTDATED;
    case DOE_VK_SURFACE_LOST: return NATIVE_SURFACE_LOST;
    }
    return NATIVE_FAILURE;
}

native_result native_from_metal(doe_mtl_status status)
{
    switch (status) {
    case DOE_MTL_OK: return NATIVE_OK;
    case DOE_MTL_PENDING: return NATIVE_PENDING;
    case DOE_MTL_COMPLETION_UNKNOWN: return NATIVE_UNKNOWN;
    case DOE_MTL_OUT_OF_MEMORY: return NATIVE_OUT_OF_MEMORY;
    case DOE_MTL_LOADER_UNAVAILABLE: return NATIVE_UNAVAILABLE;
    case DOE_MTL_UNSUPPORTED: return NATIVE_UNSUPPORTED;
    case DOE_MTL_NATIVE_FAILURE:
    case DOE_MTL_EXECUTION_FAILED: return NATIVE_FAILURE;
    case DOE_MTL_BUSY: return NATIVE_BUSY;
    case DOE_MTL_INVALID_STATE:
    case DOE_MTL_UNINITIALIZED: return NATIVE_INVALID_STATE;
    case DOE_MTL_INVALID_ARGUMENT:
    case DOE_MTL_RANGE:
    case DOE_MTL_ALIGNMENT:
    case DOE_MTL_DEVICE_MISMATCH: return NATIVE_INVALID_ARGUMENT;
    }
    return NATIVE_FAILURE;
}

native_result native_from_d3d12(doe_dx_status status)
{
    switch (status) {
    case DOE_DX_OK: return NATIVE_OK;
    case DOE_DX_PENDING: return NATIVE_PENDING;
    case DOE_DX_COMPLETION_UNKNOWN: return NATIVE_UNKNOWN;
    case DOE_DX_INVALID_ARGUMENT:
    case DOE_DX_RANGE:
    case DOE_DX_ALIGNMENT:
    case DOE_DX_DEVICE_MISMATCH: return NATIVE_INVALID_ARGUMENT;
    case DOE_DX_INVALID_STATE:
    case DOE_DX_UNINITIALIZED: return NATIVE_INVALID_STATE;
    case DOE_DX_OUT_OF_MEMORY: return NATIVE_OUT_OF_MEMORY;
    case DOE_DX_UNAVAILABLE: return NATIVE_UNAVAILABLE;
    case DOE_DX_UNSUPPORTED: return NATIVE_UNSUPPORTED;
    case DOE_DX_NATIVE_FAILURE: return NATIVE_FAILURE;
    case DOE_DX_DEVICE_LOST: return NATIVE_DEVICE_LOST;
    case DOE_DX_BUSY: return NATIVE_BUSY;
    }
    return NATIVE_FAILURE;
}

doe_vk_status native_to_vulkan(native_result status)
{
    switch (status) {
    case NATIVE_OK: return DOE_VK_OK;
    case NATIVE_PENDING:
    case NATIVE_UNKNOWN: return DOE_VK_PENDING;
    case NATIVE_TIMEOUT: return DOE_VK_TIMEOUT;
    case NATIVE_INVALID_ARGUMENT: return DOE_VK_INVALID_ARGUMENT;
    case NATIVE_OUT_OF_MEMORY: return DOE_VK_OUT_OF_MEMORY;
    case NATIVE_UNAVAILABLE: return DOE_VK_LOADER_UNAVAILABLE;
    case NATIVE_UNSUPPORTED: return DOE_VK_UNSUPPORTED;
    case NATIVE_FAILURE: return DOE_VK_NATIVE_FAILURE;
    case NATIVE_DEVICE_LOST: return DOE_VK_DEVICE_LOST;
    case NATIVE_BUSY: return DOE_VK_BUSY;
    case NATIVE_INVALID_STATE: return DOE_VK_INVALID_STATE;
    case NATIVE_SURFACE_OUTDATED: return DOE_VK_SURFACE_OUTDATED;
    case NATIVE_SURFACE_LOST: return DOE_VK_SURFACE_LOST;
    }
    return DOE_VK_NATIVE_FAILURE;
}

WGPUErrorType native_result_error(native_result status)
{
    switch (status) {
    case NATIVE_OK: return WGPUErrorType_NoError;
    case NATIVE_OUT_OF_MEMORY: return WGPUErrorType_OutOfMemory;
    case NATIVE_INVALID_ARGUMENT:
    case NATIVE_UNSUPPORTED:
    case NATIVE_BUSY:
    case NATIVE_INVALID_STATE: return WGPUErrorType_Validation;
    case NATIVE_PENDING:
    case NATIVE_UNKNOWN:
    case NATIVE_TIMEOUT:
    case NATIVE_UNAVAILABLE:
    case NATIVE_FAILURE:
    case NATIVE_DEVICE_LOST:
    case NATIVE_SURFACE_OUTDATED:
    case NATIVE_SURFACE_LOST: return WGPUErrorType_Internal;
    }
    return WGPUErrorType_Internal;
}
