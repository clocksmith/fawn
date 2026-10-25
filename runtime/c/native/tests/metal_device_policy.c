/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include "../../backends/metal/tests/mock_native.h"
#include "consumer.h"
static mock_native mocks[4];
static doe_mtl_context *contexts[4];
static unsigned opened, identity;
doe_mtl_status __wrap_doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error) {
    assert(opened<4);doe_mtl_status status=doe_mtl_context_open(mock_driver(&mocks[opened]),out,error);
    if(status==DOE_MTL_OK)contexts[opened++]=*out;
    return status;
}
void *__wrap_doe_mtl_context_device(const doe_mtl_context *context) {
    assert(context);return &identity;
}
doe_mtl_status __real_doe_mtl_commands_wait(doe_mtl_commands *,doe_mtl_error *);
doe_mtl_status __wrap_doe_mtl_commands_wait(doe_mtl_commands *commands,doe_mtl_error *error) {
    for(unsigned i=0;i<opened;++i)if(contexts[i]&&mocks[i].last_command)mock_result(&mocks[i],4,0);
    return __real_doe_mtl_commands_wait(commands,error);
}
static void refused(WGPURequestDeviceStatus status,WGPUDevice device,WGPUStringView message,void *data,void *unused) {
    (void)message;(void)unused;assert(status==WGPURequestDeviceStatus_Error&&!device);++*(unsigned*)data;
}
int main(void) {
    state s={0};s.instance=wgpuCreateInstance(NULL);assert(s.instance);
    WGPURequestAdapterOptions options=WGPU_REQUEST_ADAPTER_OPTIONS_INIT;options.backendType=WGPUBackendType_Metal;
    WGPURequestAdapterCallbackInfo adapter={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=adapter_reply,.userdata1=&s};
    assert(wgpuInstanceRequestAdapter(s.instance,&options,adapter).id&&s.adapter&&opened==1);
    for(unsigned upload=1;upload<=2;++upload){
        state selected={.instance=s.instance,.adapter=s.adapter};
        WGPUDeviceDescriptor descriptor=WGPU_DEVICE_DESCRIPTOR_INIT;
        descriptor.uncapturedErrorCallbackInfo=(WGPUUncapturedErrorCallbackInfo){.callback=error_reply,.userdata1=&selected};
        WGPURequestDeviceCallbackInfo callback={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=device_reply,.userdata1=&selected};
        DoeNativeDevicePolicy policy={.queue_selection=1,.synchronization=1,.upload_policy=upload};
        assert(doeNativeAdapterRequestDeviceWithPolicy(s.adapter,&descriptor,callback,1,&policy,sizeof(policy)).id&&selected.device);
        selected.queue=wgpuDeviceGetQueue(selected.device);unsigned index=opened-1;
        assert(doeNativeQueueSyncInfo(selected.queue)==DOE_NATIVE_QUEUE_SYNC_BACKEND_METAL); /* Optional event unavailable, never fabricated. */
        WGPUBuffer output=buffer(&selected,16,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
        unsigned copies=mocks[index].copies,commits=mocks[index].commits;
        const uint32_t words[4]={1,2,3,4};wgpuQueueWriteBuffer(selected.queue,output,0,words,sizeof(words));
        work_done(&selected);
        assert(mocks[index].copies==copies+(upload==1)&&mocks[index].commits==commits+(upload==1));
        assert(!selected.errors);wgpuBufferRelease(output);wgpuQueueRelease(selected.queue);wgpuDeviceRelease(selected.device);
        mock_assert_closed(&mocks[index]);contexts[index]=NULL;
    }
    unsigned failures=0;WGPURequestDeviceCallbackInfo callback={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=refused,.userdata1=&failures};
    DoeNativeDevicePolicy bad={.queue_selection=2,.synchronization=1};
    assert(doeNativeAdapterRequestDeviceWithPolicy(s.adapter,NULL,callback,1,&bad,sizeof(bad)).id);
    bad.queue_selection=1;bad.synchronization=0;
    assert(doeNativeAdapterRequestDeviceWithPolicy(s.adapter,NULL,callback,1,&bad,sizeof(bad)).id);
    bad.synchronization=1;bad.subgroup_policy=1;
    assert(doeNativeAdapterRequestDeviceWithPolicy(s.adapter,NULL,callback,1,&bad,sizeof(bad)).id);
    assert(failures==3&&opened==3);
    wgpuAdapterRelease(s.adapter);wgpuInstanceRelease(s.instance);mock_assert_closed(&mocks[0]);
    puts("PASS public Metal policy exact device identity, staged blit versus mapped upload, truthful unavailable timeline, and unsupported policy rejection; typed host ledger only");
}
