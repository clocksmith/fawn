/* SPDX-License-Identifier: MIT
 * Public timestamp path, with independent native raw-query observation only in
 * this test. Production conversion remains entirely in the GPU command stream. */
#include "../src/internal.h"
#include "consumer.h"

static uint64_t expected_ns(uint64_t ticks, float period, uint32_t valid_bits)
{
    ticks &= UINT64_MAX >> (64 - valid_bits);
    uint32_t bits;
    memcpy(&bits,&period,sizeof(bits));
    uint32_t exponent = (bits >> 23) & 255;
    uint32_t multiplier = (bits & UINT32_C(0x7fffff)) | (exponent ? UINT32_C(1) << 23 : 0);
    int shift = exponent ? (int)exponent - 150 : -149;
    uint64_t low = (uint64_t)(uint32_t)ticks * multiplier;
    uint64_t high = (ticks >> 32) * multiplier + (low >> 32);
    uint32_t limbs[3] = {(uint32_t)low,(uint32_t)high,(uint32_t)(high >> 32)};
    uint64_t result = 0;
    for (int bit = 0; bit < 64; ++bit) {
        int source = bit - shift;
        if (source >= 0 && source < 96 && (limbs[source / 32] & (UINT32_C(1) << (source % 32))))
            result |= UINT64_C(1) << bit;
    }
    return result;
}
static void read_raw(native_device *device, doe_vk_query_pool *pool, uint64_t values[4])
{
    doe_vk_buffer *buffer = NULL;
    doe_vk_commands *commands = NULL;
    native_lock(device);
    assert(doe_vk_buffer_create(device->backend.value.vulkan.context,32,DOE_VK_BUFFER_COPY_DST,DOE_VK_MEMORY_HOST,&buffer,NULL) == DOE_VK_OK);
    assert(doe_vk_commands_create(device->backend.value.vulkan.context,&commands,NULL) == DOE_VK_OK);
    assert(doe_vk_commands_resolve_queries(commands,pool,0,4,buffer,0) == DOE_VK_OK);
    assert(doe_vk_commands_submit(commands,NULL) == DOE_VK_OK);
    assert(doe_vk_commands_wait(commands,UINT64_C(5000000000),NULL) == DOE_VK_OK);
    assert(doe_vk_buffer_read(buffer,0,values,32) == DOE_VK_OK);
    assert(doe_vk_commands_release(commands) == DOE_VK_OK);
    doe_vk_buffer_release(buffer);
    native_unlock(device);
}
static WGPUQuerySet query_set(state *s, uint32_t count)
{
    WGPUQuerySetDescriptor descriptor = {.type=WGPUQueryType_Timestamp,.count=count};
    return wgpuDeviceCreateQuerySet(s->device,&descriptor);
}
static void invalid_ranges(state *s)
{
    WGPUQuerySet empty = query_set(s,0);
    assert(empty && wgpuQuerySetGetCount(empty) == 0 && wgpuQuerySetGetType(empty) == WGPUQueryType_Timestamp);
    wgpuQuerySetSetLabel(empty,(WGPUStringView){"empty",5});
    wgpuQuerySetAddRef(empty);
    wgpuQuerySetRelease(empty);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!query_set(s,DOE_RESOURCE_MAX_QUERIES+1));
    expect_validation(s);
    WGPUBuffer destination = buffer(s,256,WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderResolveQuerySet(encoder,empty,0,0,destination,0);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuQuerySetRelease(empty);
    WGPUQuerySet set = query_set(s,2);
    assert(set);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    writes.querySet = set;
    writes.beginningOfPassWriteIndex = writes.endOfPassWriteIndex = 0;
    WGPUComputePassDescriptor pass_desc = {.timestampWrites=&writes};
    assert(!wgpuCommandEncoderBeginComputePass(encoder,&pass_desc));
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderResolveQuerySet(encoder,set,0,1,destination,8);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderWriteTimestamp(encoder,set,0);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQuerySetDestroy(set);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuQueueSubmit(s->queue,1,&commands);
    expect_validation(s);
    wgpuCommandBufferRelease(commands);
    wgpuQuerySetRelease(set);
    wgpuBufferRelease(destination);
}
static void actual_queries(state *s)
{
    WGPUQuerySet set = query_set(s,4);
    assert(set);
    native_device *device = (native_device *)s->device;
    native_queries *payload = doe_resource_query_set_native((doe_resource_query_set *)set);
    doe_vk_query_pool *raw_pool = payload->pool;
    assert(doe_vk_query_pool_retain(raw_pool) == DOE_VK_OK);
    WGPUBuffer output = buffer(s,512,WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    uint64_t sentinel[64];
    for (size_t i = 0; i < 64; ++i) sentinel[i] = UINT64_MAX;
    wgpuQueueWriteBuffer(s->queue,output,0,sentinel,sizeof(sentinel));
    WGPUBuffer readback = buffer(s,56,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUComputePipeline compute = pipeline(s,"@compute @workgroup_size(1) fn main(){}");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderWriteTimestamp(encoder,set,0);
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    writes.querySet = set;
    writes.beginningOfPassWriteIndex = 1;
    writes.endOfPassWriteIndex = 2;
    WGPUComputePassDescriptor pass_desc = {.timestampWrites=&writes};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,&pass_desc);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass,compute);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuComputePipelineRelease(compute);
    wgpuCommandEncoderResolveQuerySet(encoder,set,0,4,output,0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,32);
    wgpuCommandEncoderResolveQuerySet(encoder,set,2,2,output,256);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,256,readback,32,24);
    /* Valid zero-count resolve changes no bytes, but still checks first<count. */
    wgpuCommandEncoderResolveQuerySet(encoder,set,3,0,output,0);
    wgpuQuerySetRelease(set);
    wgpuBufferRelease(output);
    submit(s,encoder);
    uint64_t raw[4];
    read_raw(device,raw_pool,raw);
    doe_vk_query_pool_release(raw_pool);
    const uint64_t *values = map(s,readback,56);
    for (size_t i = 0; i < 4; ++i)
        assert(values[i] == expected_ns(raw[i],device->backend.value.vulkan.caps.timestamp_period_ns,device->backend.value.vulkan.caps.timestamp_valid_bits));
    assert(raw[0] && raw[1] && raw[2] && !raw[3]);
    assert(values[4] == values[2] && values[5] == 0 && values[6] == UINT64_MAX);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
int main(void)
{
    state s = {0};
    s.instance = wgpuCreateInstance(NULL);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id && s.adapter);
    adapter_class_require(s.adapter);
    assert(wgpuAdapterHasFeature(s.adapter,WGPUFeatureName_TimestampQuery));
    WGPUSupportedFeatures features = {0};
    wgpuAdapterGetFeatures(s.adapter,&features);
    /* The adapter list holds timestamp-query among any other offered features,
     * each agreeing with HasFeature (format_features.c checks the others). */
    bool listed = false;
    for (size_t i = 0; i < features.featureCount; ++i) {
        assert(wgpuAdapterHasFeature(s.adapter,features.features[i]));
        listed |= features.features[i] == WGPUFeatureName_TimestampQuery;
    }
    assert(listed);
    wgpuSupportedFeaturesFreeMembers(features);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&s};
    WGPURequestDeviceCallbackInfo requested = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&s};
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    assert(!wgpuDeviceHasFeature(s.device,WGPUFeatureName_TimestampQuery));
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!query_set(&s,2));
    expect_validation(&s);
    wgpuDeviceGetFeatures(s.device,&features);
    assert(!features.featureCount);
    wgpuSupportedFeaturesFreeMembers(features);
    wgpuDeviceRelease(s.device);
    WGPUFeatureName required = WGPUFeatureName_TimestampQuery;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &required;
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    assert(wgpuDeviceHasFeature(s.device,required));
    wgpuDeviceGetFeatures(s.device,&features);
    assert(features.featureCount == 1 && features.features[0] == required);
    wgpuSupportedFeaturesFreeMembers(features);
    s.queue = wgpuDeviceGetQueue(s.device);
    invalid_ranges(&s);
    actual_queries(&s);
    assert(!s.errors && s.validation_errors == 6);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public timestamps: requested feature, zero/max/range/destroy admission, real pass/command writes, exact GPU nanoseconds, same-batch copy, scratch reuse and early query release %s\n", adapter_class_scope());
}
