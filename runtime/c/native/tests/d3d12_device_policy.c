/* SPDX-License-Identifier: MIT
 * Typed admission boundary only. The existing Windows queue/fence/upload owner
 * is not replaced or simulated; this test does not claim Windows execution. */
#include "../src/backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const native_backend *expected;
static unsigned reopens;
static native_result reopen_result;
native_result __wrap_native_d3d12_reopen(const native_backend *adapter,native_backend *out) {
    assert(adapter==expected);++reopens;
    if(reopen_result==NATIVE_OK)*out=*adapter;
    return reopen_result;
}
int main(void) {
    native_backend adapter={.kind=WGPUBackendType_D3D12};expected=&adapter;
    adapter.value.d3d12.caps.luid_low=123;adapter.value.d3d12.caps.luid_high=-7;
    for(unsigned queue=0;queue<=1;++queue)for(unsigned upload=0;upload<=1;++upload){
        native_backend out={0};DoeNativeDevicePolicy policy={.queue_selection=queue,.synchronization=1,.upload_policy=upload};
        assert(native_backend_device_policy(&adapter,&policy,&out)==NATIVE_OK);
        assert(out.kind==WGPUBackendType_D3D12&&out.upload_policy==upload&&
            out.value.d3d12.caps.luid_low==123&&out.value.d3d12.caps.luid_high==-7);
        assert(!adapter.upload_policy);
    }
    native_backend out={.kind=WGPUBackendType_Metal};
    assert(native_backend_device_policy(&adapter,NULL,&out)==NATIVE_OK&&out.kind==WGPUBackendType_D3D12);
    const DoeNativeDevicePolicy invalid[]={
        {.queue_selection=2,.synchronization=1}, {.queue_selection=1,.synchronization=0},
        {.queue_selection=1,.synchronization=1,.upload_policy=2},
        {.queue_selection=1,.synchronization=1,.subgroup_policy=1},
        {.queue_selection=1,.synchronization=1,.reserved={1}}
    };
    unsigned before=reopens;
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
        memset(&out,0xa5,sizeof(out));native_backend unchanged=out;
        assert(native_backend_device_policy(&adapter,&invalid[i],&out)==NATIVE_UNSUPPORTED);
        assert(!memcmp(&out,&unchanged,sizeof(out))&&reopens==before);
    }
    DoeNativeDevicePolicy policy={.queue_selection=1,.synchronization=1,.upload_policy=1};
    reopen_result=NATIVE_DEVICE_LOST;memset(&out,0xa5,sizeof(out));native_backend unchanged=out;
    assert(native_backend_device_policy(&adapter,&policy,&out)==NATIVE_DEVICE_LOST);
    assert(!memcmp(&out,&unchanged,sizeof(out)));
    puts("PASS D3D12 exact direct-queue/timeline/staged policy admission, same-adapter reopen, first failure/output preservation and unsupported tuples; host boundary only");
}
