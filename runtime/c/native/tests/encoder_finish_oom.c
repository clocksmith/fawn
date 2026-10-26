/* SPDX-License-Identifier: MIT
 * A real allocation failure at Finish remains OutOfMemory at that call. Once
 * finished, the same encoder's later operations report Validation instead of
 * replaying its sticky recording failure into the wrong error-scope filter. */
#include "consumer.h"
#include <errno.h>

static _Thread_local bool fail_next_allocation;
static _Thread_local unsigned failures;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size)
{
    if (fail_next_allocation) {
        fail_next_allocation = false;
        ++failures;
        errno = ENOMEM;
        return NULL;
    }
    return __real_malloc(size);
}

typedef struct scope_result {
    WGPUErrorType expected;
    unsigned calls;
} scope_result;
static void scope_reply(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    scope_result *result = userdata;
    assert(status == WGPUPopErrorScopeStatus_Success);
    assert(type == result->expected);
    if (type != WGPUErrorType_NoError) assert(message.data && message.length);
    ++result->calls;
}
static void expect_scope(state *s, WGPUErrorType expected)
{
    scope_result result = {.expected=expected};
    WGPUPopErrorScopeCallbackInfo info = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=scope_reply,.userdata1=&result};
    assert(wgpuDevicePopErrorScope(s->device, info).id && result.calls == 1);
    assert(!s->errors);
}
static void push_filters(state *s)
{
    /* An OOM cannot accidentally satisfy the inner Validation filter. */
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
}
static void expect_filters(state *s, bool first_finish)
{
    expect_scope(s, first_finish ? WGPUErrorType_NoError : WGPUErrorType_Validation);
    expect_scope(s, first_finish ? WGPUErrorType_OutOfMemory : WGPUErrorType_NoError);
}
static void command_encoder(state *s)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    assert(encoder);
    push_filters(s);
    unsigned before = failures;
    fail_next_allocation = true;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(!fail_next_allocation && failures == before + 1);
    assert(commands); /* The rejected command buffer has its ordinary lifetime. */
    wgpuCommandBufferRelease(commands);
    expect_filters(s, true);

    push_filters(s);
    wgpuCommandEncoderInsertDebugMarker(encoder, (WGPUStringView){"after Finish", WGPU_STRLEN});
    expect_filters(s, false);
    push_filters(s);
    wgpuCommandEncoderInsertDebugMarker(encoder, (WGPUStringView){NULL, 1});
    expect_filters(s, false);
    push_filters(s);
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandBufferRelease(commands);
    expect_filters(s, false);
    wgpuCommandEncoderRelease(encoder);
}
static void bundle_encoder(state *s)
{
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    descriptor.colorFormatCount = 1;
    descriptor.colorFormats = &format;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(s->device, &descriptor);
    assert(encoder);
    push_filters(s);
    unsigned before = failures;
    fail_next_allocation = true;
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(encoder, NULL);
    assert(!fail_next_allocation && failures == before + 1);
    assert(bundle);
    wgpuRenderBundleRelease(bundle);
    expect_filters(s, true);

    push_filters(s);
    wgpuRenderBundleEncoderInsertDebugMarker(encoder, (WGPUStringView){"after Finish", WGPU_STRLEN});
    expect_filters(s, false);
    push_filters(s);
    wgpuRenderBundleEncoderInsertDebugMarker(encoder, (WGPUStringView){NULL, 1});
    expect_filters(s, false);
    push_filters(s);
    wgpuRenderBundleEncoderSetBindGroup(encoder, 0, NULL, 0, NULL);
    expect_filters(s, false);
    push_filters(s);
    bundle = wgpuRenderBundleEncoderFinish(encoder, NULL);
    assert(bundle);
    wgpuRenderBundleRelease(bundle);
    expect_filters(s, false);
    wgpuRenderBundleEncoderRelease(encoder);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    command_encoder(&s);
    bundle_encoder(&s);
    assert(failures == 2);
    consumer_finish(&s);
    printf("PASS: Finish allocation OOM, then finished-state Validation (%s)\n", adapter_class_scope());
    return 0;
}
