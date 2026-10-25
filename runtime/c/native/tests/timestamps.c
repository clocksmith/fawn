/* SPDX-License-Identifier: MIT
 * Public timestamp path, with independent native raw-query observation only in
 * this test. Production conversion remains entirely in the GPU command stream. */
#define _POSIX_C_SOURCE 200809L
#include "../src/internal.h"
#include <time.h>
#include <inttypes.h>
#include "consumer.h"
#include "../../common/include/doe_timestamp.h"

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
static void read_raw_vulkan(native_device *device, doe_vk_query_pool *pool, uint64_t values[4])
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
static void read_raw_metal(native_device *device, doe_mtl_timestamps *timestamps, uint64_t values[4])
{
    doe_mtl_buffer *buffer = NULL;
    doe_mtl_commands *commands = NULL;
    native_lock(device);
    assert(doe_mtl_buffer_create(device->backend.value.metal.context, 32, DOE_MTL_SHARED, &buffer, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_create(device->backend.value.metal.context, &commands, NULL) == DOE_MTL_OK);
    /* Three actual samples; index3 is deliberately unwritten. Strict private
     * resolve refuses unavailable indices, independently of public zero policy. */
    assert(doe_mtl_commands_resolve_timestamps(commands, timestamps, 0, 3, buffer, 0, NULL) == DOE_MTL_OK);
    assert(doe_mtl_commands_submit(commands, NULL) == DOE_MTL_OK);
    struct timespec start, now, pause = {0,1000000};
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    doe_mtl_status status;
    while ((status = doe_mtl_commands_poll(commands, NULL)) == DOE_MTL_PENDING || status == DOE_MTL_COMPLETION_UNKNOWN) {
        assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
        if (now.tv_sec - start.tv_sec >= 5) {
            fputs("raw timestamp deadline; command leases retained\n", stderr);
            exit(3);
        }
        (void)nanosleep(&pause, NULL);
    }
    assert(status == DOE_MTL_OK);
    assert(doe_mtl_buffer_read(buffer, 0, values, 32) == DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands) == DOE_MTL_OK);
    doe_mtl_buffer_release(buffer);
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
    finish_invalid(encoder);
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
    WGPUComputePassEncoder rejected = wgpuCommandEncoderBeginComputePass(encoder,&pass_desc);
    assert(rejected); /* WebGPU invalid pass; the error also surfaces at Finish. */
    wgpuComputePassEncoderDispatchWorkgroups(rejected,1,1,1);
    wgpuComputePassEncoderEnd(rejected);
    wgpuComputePassEncoderRelease(rejected);
    finish_invalid(encoder);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderResolveQuerySet(encoder,set,0,1,destination,8);
    finish_invalid(encoder);
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
    native_queries raw_query = *payload;
    bool metal = device->backend.kind == WGPUBackendType_Metal;
    if (metal) assert(doe_mtl_timestamps_retain(raw_query.metal_timestamps) == DOE_MTL_OK);
    else assert(doe_vk_query_pool_retain(raw_query.pool) == DOE_VK_OK);
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
    if (metal) {
        read_raw_metal(device, raw_query.metal_timestamps, raw);
        doe_mtl_timestamps_release(raw_query.metal_timestamps);
    } else {
        read_raw_vulkan(device, raw_query.pool, raw);
        doe_vk_query_pool_release(raw_query.pool);
    }
    float period = metal ? device->backend.value.metal.caps.timestamp_period_ns :
        device->backend.value.vulkan.caps.timestamp_period_ns;
    uint32_t valid_bits = metal ? 64 : device->backend.value.vulkan.caps.timestamp_valid_bits;
    const uint64_t *values = map(s,readback,56);
    for (size_t i = 0; i < 4; ++i)
        assert(values[i] == expected_ns(raw[i],period,valid_bits));
    assert(raw[0] && raw[1] && raw[2] && !raw[3]);
    assert(values[0] <= values[1] && values[1] <= values[2]);
    printf("calibrated ns/tick=%.9g raw=%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", (double)period,raw[0],raw[1],raw[2]);
    assert(values[4] == values[2] && values[5] == 0 && values[6] == UINT64_MAX);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void normalization_values(state *s)
{
    const uint64_t input[] = {0,1,2,3,UINT32_MAX,UINT64_C(0x10000000000001),
        UINT64_C(0x8000000000000000),UINT64_MAX};
    char source[DOE_TIMESTAMP_SOURCE_CAPACITY];
    size_t length = 0;
    assert(doe_timestamp_source_mode(2.5f, 64, 64, true, source, sizeof(source), &length) == DOE_TIMESTAMP_SOURCE_OK);
    WGPUComputePipeline compute = pipeline(s, source);
    WGPUBuffer data = buffer(s, sizeof(input), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, sizeof(input), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, data, 0, input, sizeof(input));
    WGPUBindGroupEntry entry = {.binding=0,.buffer=data,.size=sizeof(input)};
    WGPUBindGroup bind = group(s, compute, &entry, 1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    dispatch(encoder, compute, bind);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, data, 0, readback, 0, sizeof(input));
    wgpuBindGroupRelease(bind);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(data);
    submit(s, encoder);
    const uint64_t *actual = map(s, readback, sizeof(input));
    for (size_t i = 0; i < sizeof(input) / sizeof(input[0]); ++i)
        assert(actual[i] == (input[i] == UINT64_MAX ? UINT64_MAX : expected_ns(input[i], 2.5f, 64)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void render_and_reuse(state *s)
{
    WGPUQuerySet set = query_set(s, 2);
    WGPUTexture image = texture(s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_RenderAttachment, 1);
    WGPUTextureView view = wgpuTextureCreateView(image, NULL);
    WGPUBuffer result = buffer(s, 16, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, 16, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    uint64_t previous[2] = {0};
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        writes.querySet = set;
        if (iteration != 1) writes.beginningOfPassWriteIndex = 0;
        if (iteration != 2) writes.endOfPassWriteIndex = 1;
        if (iteration == 2) {
            WGPUComputePassDescriptor desc = {.timestampWrites=&writes};
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &desc);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        } else {
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = view; color.loadOp = WGPULoadOp_Clear; color.storeOp = WGPUStoreOp_Store;
            color.clearValue = (WGPUColor){0,1,0,1};
            WGPURenderPassDescriptor desc = {.colorAttachmentCount=1,.colorAttachments=&color,.timestampWrites=&writes};
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
        wgpuCommandEncoderResolveQuerySet(encoder, set, 0, 2, result, 0);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, result, 0, readback, 0, 16);
        submit(s, encoder);
        const uint64_t *values = map(s, readback, 16);
        assert(values[0] && values[1]);
        if (iteration == 0) assert(values[1] >= values[0]);
        if (iteration == 1) assert(values[0] == previous[0] && values[1] > previous[1]);
        if (iteration == 2) assert(values[0] > previous[1] && values[1] == previous[1]);
        memcpy(previous, values, sizeof(previous));
        wgpuBufferUnmap(readback);
    }
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    for (unsigned i = 0; i < 2; ++i) {
        wgpuCommandEncoderWriteTimestamp(encoder, set, 0);
        wgpuCommandEncoderResolveQuerySet(encoder, set, 0, 1, result, 0);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, result, 0, readback, (uint64_t)i * 8, 8);
    }
    submit(s, encoder);
    const uint64_t *snapshots = map(s, readback, 16);
    assert(snapshots[0] > previous[0] && snapshots[1] > snapshots[0]);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback); wgpuBufferRelease(result);
    wgpuTextureViewRelease(view); wgpuTextureRelease(image); wgpuQuerySetRelease(set);
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
    assert(features.featureCount == 1 && features.features[0] == WGPUFeatureName_CoreFeaturesAndLimits);
    wgpuSupportedFeaturesFreeMembers(features);
    wgpuDeviceRelease(s.device);
    WGPUFeatureName required = WGPUFeatureName_TimestampQuery;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &required;
    assert(wgpuAdapterRequestDevice(s.adapter,&descriptor,requested).id && s.device);
    assert(wgpuDeviceHasFeature(s.device,required));
    wgpuDeviceGetFeatures(s.device,&features);
    /* Listed in ascending enum order after the automatic core feature. */
    assert(features.featureCount == 2 && features.features[0] == WGPUFeatureName_CoreFeaturesAndLimits &&
        features.features[1] == required);
    wgpuSupportedFeaturesFreeMembers(features);
    s.queue = wgpuDeviceGetQueue(s.device);
    invalid_ranges(&s);
    actual_queries(&s);
    normalization_values(&s);
    render_and_reuse(&s);
    assert(!s.errors && s.validation_errors == 6);
    wgpuQueueRelease(s.queue);
    wgpuDeviceRelease(s.device);
    wgpuAdapterRelease(s.adapter);
    wgpuInstanceRelease(s.instance);
    printf("PASS public timestamps: requested feature, zero/max/range/destroy admission, real pass/command writes, exact GPU nanoseconds, same-batch copy, error-sentinel normalization, render/empty passes, one-sided writes, same-recording/cross-submit reuse and early query release %s\n", adapter_class_scope());
}
