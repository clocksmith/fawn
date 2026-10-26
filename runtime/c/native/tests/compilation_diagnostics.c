/* SPDX-License-Identifier: MIT
 * Public deferred compilation diagnostics keep immutable module-owned text alive. */
#include "consumer.h"
#include <stdlib.h>

static bool fail_messages;
void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_messages && count && size == sizeof(WGPUCompilationMessage)) {
        fail_messages = false;
        return NULL;
    }
    return __real_calloc(count, size);
}
typedef struct report {
    WGPUCompilationMessageType type;
    unsigned callbacks;
    size_t count;
} report;
static void compilation(WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info,
    void *userdata, void *unused)
{
    (void)unused;
    report *value = userdata;
    assert(status == WGPUCompilationInfoRequestStatus_Success && info);
    assert(info->messageCount == value->count);
    for (size_t i = 0; i < info->messageCount; i++) {
        const WGPUCompilationMessage *message = &info->messages[i];
        assert(message->type == value->type);
        assert(message->message.data && message->message.length);
        assert(message->lineNum && message->linePos);
    }
    value->callbacks++;
}
static WGPUShaderModule create(state *s, const char *severity)
{
    char source[512];
    int length = snprintf(source, sizeof(source),
        "diagnostic(%s, derivative_uniformity);\n"
        "@fragment fn main(@location(0) v: f32) -> @location(0) f32 {\n"
        " if (v > 0.0) { return dpdx(v); } return 0.0; }", severity);
    assert(length > 0 && (size_t)length < sizeof(source));
    WGPUShaderSourceWGSL wgsl = {.chain={.sType=WGPUSType_ShaderSourceWGSL},
        .code={source,(size_t)length}};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&wgsl.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    memset(source, 'x', (size_t)length);
    return module;
}
static void scoped_oom(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_OutOfMemory);
    ++*(unsigned *)userdata;
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    const char *levels[] = {"warning", "info", "off"};
    for (size_t i = 0; i < 3; i++) {
        WGPUShaderModule shader = create(&s, levels[i]);
        assert(shader);
        report value = {.type=i == 1 ? WGPUCompilationMessageType_Info :
            WGPUCompilationMessageType_Warning, .count=i == 2 ? 0 : 1};
        WGPUCompilationInfoCallbackInfo callback = {.mode=WGPUCallbackMode_AllowProcessEvents,
            .callback=compilation, .userdata1=&value};
        assert(wgpuShaderModuleGetCompilationInfo(shader, callback).id);
        assert(!value.callbacks);
        wgpuShaderModuleRelease(shader);
        wgpuInstanceProcessEvents(s.instance);
        assert(value.callbacks == 1);
    }
    unsigned errors = 0;
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_OutOfMemory);
    fail_messages = true;
    assert(!create(&s, "warning") && !fail_messages);
    WGPUPopErrorScopeCallbackInfo scope = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=scoped_oom, .userdata1=&errors};
    assert(wgpuDevicePopErrorScope(s.device, scope).id && errors == 1);
    consumer_finish(&s);
    puts("compilation diagnostics: warning/info/off, deferred early release and projection OOM passed");
    return 0;
}
