/* SPDX-License-Identifier: MIT
 * Real staging allocation, host range and transfer owners; mock native calls,
 * not GPU execution. Only error delivery and the public submit boundary inject. */
#include "../src/metal_frontend.h"
#include "../../backends/metal/tests/mock_native.h"
#include "../../resources/tests/fixture.h"

static native_device *active;
static mock_native *ledger;
static unsigned errors, submits;
static WGPUErrorType last_error;
static unsigned char data[100];
void metal_staging_error(native_device *device, WGPUErrorType type, const char *message) {
    assert(device == active && message && *message);
    ++errors; last_error = type;
}
void metal_staging_submit(WGPUQueue queue, size_t count, const WGPUCommandBuffer *commands) {
    assert(queue == (WGPUQueue)active && count == 1 && commands && commands[0]);
    ++submits;
    doe_resource_commands *resources = (doe_resource_commands *)commands[0];
    size_t records_count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(resources, &records_count);
    assert(records_count == 1 && records[0].kind == DOE_COMMAND_BUFFER_TEXTURE);
    const doe_resource_texture_copy *copy = &records[0].value.texture.copy;
    assert(!copy->offset && copy->bytes_per_row == 256 && copy->rows_per_image == 2);
    native_buffer *buffer = doe_resource_buffer_native(records[0].value.texture.buffer);
    assert(wgpuBufferGetSize((WGPUBuffer)records[0].value.texture.buffer) == 1024);
    assert(ledger->content_queries == 2); /* Real Shared creation zero + direct writable range, no intermediate upload. */
    unsigned char bytes[1024];
    assert(native_buffer_read(buffer, 0, bytes, sizeof(bytes)).status == NATIVE_OK);
    for (size_t z = 0; z < 2; ++z) for (size_t y = 0; y < 2; ++y) {
        size_t row = (z * 2 + y) * 256;
        assert(!memcmp(bytes + row, data + 4 + (z * 3 + y) * 20, 12));
        for (size_t i = 12; i < 256; ++i) assert(!bytes[row + i]);
    }
    assert(doe_resource_commands_begin_submit(resources) == DOE_RESOURCE_OK);
    doe_mtl_commands *native_commands = NULL;
    assert(doe_mtl_commands_create(active->backend.value.metal.context, &native_commands, NULL) == DOE_MTL_OK);
    assert(native_metal_record_commands(native_commands, resources).status == NATIVE_OK);
    assert(ledger->uploads == 1 && !ledger->renders && !ledger->views);
    assert(doe_mtl_commands_submit(native_commands, NULL) == DOE_MTL_OK);
    mock_result(ledger, 4, 0);
    assert(doe_mtl_commands_poll(native_commands, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(native_commands) == DOE_MTL_OK);
    assert(doe_resource_commands_settle(resources, DOE_RESOURCE_OK) == DOE_RESOURCE_OK);
}
static bool alive(void *identity) { return !((native_device *)identity)->destroyed; }
static void texture_release(void *userdata, void *handle) {
    (void)userdata;
    native_texture *texture = handle;
    doe_mtl_texture_release(texture->metal);
}
int main(void) {
    fixture f = fresh();
    native_device device = {0};
    atomic_init(&device.references, 1);
    assert(doe_mutex_init(&device.mutex, true).status == DOE_HOST_OK);
    mock_native mock;
    assert(doe_mtl_context_open(mock_driver(&mock), &device.backend.value.metal.context, NULL) == DOE_MTL_OK);
    device.backend.kind = WGPUBackendType_Metal;
    device.backend.value.metal.caps.max_buffer_length = 4096;
    device.resources = config(&f);
    device.resources.device = (doe_resource_device){&device,alive,native_device_retain,native_device_release};
    native_texture payload = {.device=&device};
    doe_mtl_texture_desc native_desc = {DOE_MTL_TEXTURE_2D_ARRAY,DOE_MTL_RGBA8_UNORM,3,2,2,1,
        DOE_MTL_TEXTURE_COPY_DST | DOE_MTL_TEXTURE_RENDER_TARGET,false,1};
    assert(doe_mtl_texture_create(device.backend.value.metal.context, &native_desc, &payload.metal, NULL) == DOE_MTL_OK);
    doe_resource_texture_desc desc = texture_desc();
    desc.width = 3; desc.height = 2; desc.depth_or_layers = 2; desc.usage = WGPUTextureUsage_CopyDst;
    doe_resource_texture_limits limits = texture_limits();
    doe_resource_texture *texture = NULL;
    assert(doe_resource_texture_create(&device.resources, &desc, &limits,
        (doe_resource_native){&payload,NULL,texture_release}, &texture) == DOE_RESOURCE_OK);
    active = &device; ledger = &mock;
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)i;
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = (WGPUTexture)texture;
    WGPUTexelCopyBufferLayout layout = {.offset=4,.bytesPerRow=20,.rowsPerImage=3};
    WGPUExtent3D extent = {3,2,2};
    wgpuQueueWriteTexture((WGPUQueue)&device, &destination, data, sizeof(data), &layout, &extent);
    assert(submits == 1 && !errors && f.live == 1 && atomic_load(&device.references) == 2);
    unsigned before = mock.created;
    extent = (WGPUExtent3D){0,1,1}; layout.offset = 0;
    wgpuQueueWriteTexture((WGPUQueue)&device, &destination, NULL, 0, &layout, &extent);
    assert(submits == 1 && !errors && mock.created == before);
    extent = (WGPUExtent3D){3,2,2}; layout.offset = 4;
    mock.fail = MOCK_BUFFER;
    wgpuQueueWriteTexture((WGPUQueue)&device, &destination, data, sizeof(data), &layout, &extent);
    assert(submits == 1 && errors == 1 && last_error == WGPUErrorType_Internal);
    mock.fail = MOCK_NONE;
    f.fail_at = f.allocations;
    wgpuQueueWriteTexture((WGPUQueue)&device, &destination, data, sizeof(data), &layout, &extent);
    assert(submits == 1 && errors == 2 && last_error == WGPUErrorType_OutOfMemory);
    f.fail_at = SIZE_MAX;
    layout.bytesPerRow = 8;
    before = mock.created;
    wgpuQueueWriteTexture((WGPUQueue)&device, &destination, data, sizeof(data), &layout, &extent);
    assert(submits == 1 && errors == 3 && last_error == WGPUErrorType_Validation && mock.created == before);
    doe_resource_texture_release(texture);
    assert(atomic_load(&device.references) == 1);
    doe_mtl_context_release(device.backend.value.metal.context);
    assert(doe_mutex_destroy(&device.mutex).status == DOE_HOST_OK);
    assert(!f.live); /* Device's baseline stack lease deliberately remains. */
    mock_assert_closed(&mock);
    puts("PASS Metal queue texture staging: real Shared writable allocation, direct padded rows, full-overwrite transfer, typed single errors and empty admission; host only");
}
