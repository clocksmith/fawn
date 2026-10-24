/* SPDX-License-Identifier: MIT
 * wgpuQueueWriteTexture is bounded by the texture and its data layout, not by
 * maxBufferSize: a 1x2 rgba8unorm write whose bytesPerRow places row 1 past
 * the default 256 MiB limit stages internally and lands exactly, while a
 * public buffer of that size is still a validation error. */
#include "consumer.h"
#include <stdlib.h>

enum { DEFAULT_MAX_BUFFER = 268435456 };

static void no_error(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)unused;
    if (type != WGPUErrorType_NoError) fprintf(stderr, "unexpected: %.*s\n", (int)message.length, message.data);
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
    ++*(unsigned *)userdata;
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s.device, &limits) == WGPUStatus_Success);
    assert(limits.maxBufferSize == DEFAULT_MAX_BUFFER);
    const uint32_t bytes_per_row = DEFAULT_MAX_BUFFER; /* A multiple of 256. */
    const size_t offset = 8, required = (size_t)bytes_per_row + 4;
    const size_t size = offset + required;
    unsigned char *data = calloc(1, size);
    assert(data);
    const unsigned char row0[4] = {11, 22, 33, 44}, row1[4] = {55, 66, 77, 88};
    memcpy(data + offset, row0, 4);
    memcpy(data + offset + bytes_per_row, row1, 4);

    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    descriptor.size = (WGPUExtent3D){1, 2, 1};
    WGPUTexture target = wgpuDeviceCreateTexture(s.device, &descriptor);
    assert(target);
    WGPUTexelCopyTextureInfo destination = {.texture = target, .aspect = WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.offset = offset, .bytesPerRow = bytes_per_row, .rowsPerImage = 2};
    WGPUExtent3D extent = {1, 2, 1};
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    wgpuQueueWriteTexture(s.queue, &destination, data, size, &layout, &extent);
    unsigned popped = 0;
    WGPUPopErrorScopeCallbackInfo callback = {.mode = WGPUCallbackMode_AllowSpontaneous, .callback = no_error,
        .userdata1 = &popped};
    assert(wgpuDevicePopErrorScope(s.device, callback).id && popped == 1);
    free(data);

    WGPUBuffer readback = buffer(&s, 512, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    texture_to_buffer(encoder, target, readback, 0, 1, 2);
    submit(&s, encoder);
    const unsigned char *texels = map(&s, readback, 512);
    assert(memcmp(texels, row0, 4) == 0 && memcmp(texels + 256, row1, 4) == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    assert(s.validation_errors == 0 && !s.errors);

    /* The public entry still enforces the device limit. */
    WGPUBufferDescriptor oversized = {.size = required, .usage = WGPUBufferUsage_CopySrc};
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBuffer(s.device, &oversized));
    expect_validation(&s);

    wgpuTextureRelease(target);
    consumer_finish(&s);
    printf("PASS public large texture write: %zu staged bytes past the default maxBufferSize land exactly at "
        "both rows with a layout offset; a public buffer of that size stays a validation error %s\n",
        required, adapter_class_scope());
    return 0;
}
