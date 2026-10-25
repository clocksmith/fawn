/* SPDX-License-Identifier: MIT
 * Retained unsupported shared-buffer boundary; no synthetic resource state. */
#include "doe_native_shared_memory.h"

WGPUSharedBufferMemory wgpuDeviceImportSharedBufferMemory(WGPUDevice device,
    const WGPUSharedBufferMemoryDescriptor *descriptor)
{
    (void)device;
    (void)descriptor;
    return NULL;
}
WGPUStatus wgpuSharedBufferMemoryBeginAccess(WGPUSharedBufferMemory memory,
    WGPUBuffer buffer, const WGPUSharedBufferMemoryBeginAccessDescriptor *descriptor)
{
    (void)memory;
    (void)buffer;
    (void)descriptor;
    return WGPUStatus_Error;
}
WGPUBuffer wgpuSharedBufferMemoryCreateBuffer(WGPUSharedBufferMemory memory,
    const WGPUBufferDescriptor *descriptor)
{
    (void)memory;
    (void)descriptor;
    return NULL;
}
WGPUStatus wgpuSharedBufferMemoryEndAccess(WGPUSharedBufferMemory memory,
    WGPUBuffer buffer, WGPUSharedBufferMemoryEndAccessState *state)
{
    (void)memory;
    (void)buffer;
    if (state) {
        state->initialized = WGPU_FALSE;
        state->fenceCount = 0;
        state->fences = NULL;
        state->signaledValues = NULL;
    }
    return WGPUStatus_Error;
}
WGPUStatus wgpuSharedBufferMemoryGetProperties(WGPUSharedBufferMemory memory,
    WGPUSharedBufferMemoryProperties *properties)
{
    (void)memory;
    if (properties) {
        properties->size = 0;
        properties->usage = 0;
    }
    return WGPUStatus_Error;
}
WGPUBool wgpuSharedBufferMemoryIsDeviceLost(WGPUSharedBufferMemory memory)
{
    (void)memory;
    return WGPU_TRUE;
}
void wgpuSharedBufferMemorySetLabel(WGPUSharedBufferMemory memory, WGPUStringView label)
{
    (void)memory;
    (void)label;
}
void wgpuSharedBufferMemoryAddRef(WGPUSharedBufferMemory memory) { (void)memory; }
void wgpuSharedBufferMemoryRelease(WGPUSharedBufferMemory memory) { (void)memory; }
void wgpuSharedBufferMemoryEndAccessStateFreeMembers(WGPUSharedBufferMemoryEndAccessState state)
{
    (void)state;
}
