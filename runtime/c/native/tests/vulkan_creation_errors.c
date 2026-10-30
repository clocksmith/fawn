/* SPDX-License-Identifier: MIT
 * Inject native constructor failures, not replacement rendering semantics. */
#include "consumer.h"
#include "doe_vk_render.h"
#include "doe_vk_queries.h"

static unsigned injected, delivered;
static char operation[64];
static const char *expected;
static doe_vk_status fail(doe_vk_error *error, const char *name)
{
    ++injected;
    strcpy(operation, name);
    assert(error);
    *error = (doe_vk_error){DOE_VK_OUT_OF_MEMORY, -2, operation};
    return DOE_VK_OUT_OF_MEMORY;
}
/* Native render creation calls the blob-store entry point (render_pipeline.c). */
doe_vk_status __wrap_doe_vk_render_pipeline_create_blob(doe_vk_context *context,
    const doe_vk_render_pipeline_desc *desc, struct doe_vk_pipeline_blob *blob, doe_vk_pipeline **out,
    doe_vk_error *error)
{
    (void)blob;
    assert(context && desc && out && !*out);
    return fail(error, "vkCreateGraphicsPipelines");
}
doe_vk_status __wrap_doe_vk_occlusion_pool_create(doe_vk_context *context, uint32_t count,
    doe_vk_query_pool **out, doe_vk_error *error)
{
    assert(context && count && out && !*out);
    return fail(error, "vkCreateQueryPool");
}
doe_vk_status __wrap_doe_vk_query_pool_create(doe_vk_context *context, uint32_t count,
    doe_vk_query_pool **out, doe_vk_error *error)
{
    assert(context && count && out && !*out);
    return fail(error, "vkCreateQueryPool");
}
static void check_message(WGPUStringView message)
{
    assert(message.length == strlen(expected));
    assert(!memcmp(message.data, expected, message.length));
    ++delivered;
}
static void scoped(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)userdata; (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_OutOfMemory);
    check_message(message);
}
static void pop(state *s)
{
    memset(operation, 'x', sizeof(operation) - 1);
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=scoped};
    assert(wgpuDevicePopErrorScope(s->device, callback).id);
}
static void pipeline_reply(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)userdata; (void)unused;
    assert(status == WGPUCreatePipelineAsyncStatus_InternalError && !pipeline);
    check_message(message);
}
int main(void)
{
    state s = {0};
    const WGPUFeatureName feature = WGPUFeatureName_TimestampQuery;
    consumer_init_features(&s, &feature, 1);
    WGPUShaderSourceWGSL code = {.chain={.sType=WGPUSType_ShaderSourceWGSL},
        .code={"@vertex fn main()->@builtin(position) vec4f { return vec4f(0,0,0,1); } @fragment fn fragment_main()->@location(0) vec4f { return vec4f(1,0,0,1); }", WGPU_STRLEN}};
    WGPUShaderModuleDescriptor shader_descriptor = {.nextInChain=&code.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s.device, &shader_descriptor);
    assert(shader);
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.targetCount=1,.targets=&target};
    descriptor.fragment = &fragment;
    expected = "Vulkan: vkCreateGraphicsPipelines (native=-2)";
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_OutOfMemory);
    assert(!wgpuDeviceCreateRenderPipeline(s.device, &descriptor));
    pop(&s);
    WGPUCreateRenderPipelineAsyncCallbackInfo async = {.mode=WGPUCallbackMode_AllowProcessEvents,
        .callback=pipeline_reply};
    assert(wgpuDeviceCreateRenderPipelineAsync(s.device, &descriptor, async).id);
    wgpuShaderModuleRelease(shader);
    memset(operation, 'x', sizeof(operation) - 1);
    wgpuInstanceProcessEvents(s.instance);
    assert(injected == 2 && delivered == 2);
    expected = "Vulkan: vkCreateQueryPool (native=-2)";
    const WGPUQueryType types[] = {WGPUQueryType_Occlusion, WGPUQueryType_Timestamp};
    for (size_t i = 0; i < 2; i++) {
        WGPUQuerySetDescriptor query = {.type=types[i],.count=2};
        wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_OutOfMemory);
        assert(!wgpuDeviceCreateQuerySet(s.device, &query));
        pop(&s);
    }
    assert(injected == 4 && delivered == 4);
    consumer_finish(&s);
    puts("Vulkan creation errors: exact signed detail after cleanup and deferred callback passed");
    return 0;
}
