/* SPDX-License-Identifier: MIT
 * Feature names, compact slots and requested set semantics; no native calls. */
#include "../src/internal.h"
#include "doe_native_shared_memory.h"
#include <assert.h>
#include <stdio.h>

static void enumeration(native_adapter *adapter, WGPUFeatureName feature, bool expected)
{
    assert((bool)wgpuAdapterHasFeature((WGPUAdapter)adapter,feature)==expected);
    WGPUSupportedFeatures list={0};wgpuAdapterGetFeatures((WGPUAdapter)adapter,&list);
    unsigned occurrences=0;
    for(size_t i=0;i<list.featureCount;++i){
        assert((uint32_t)list.features[i]!=24 && (uint32_t)list.features[i]!=25);
        occurrences+=list.features[i]==feature;
    }
    assert(occurrences==(expected?1u:0u));wgpuSupportedFeaturesFreeMembers(list);
}
int main(void)
{
    native_adapter adapter={.backend={.kind=WGPUBackendType_Metal}};
    WGPUFeatureName names[]={(WGPUFeatureName)DOE_SHARED_FEATURE_IOSURFACE,
        (WGPUFeatureName)DOE_SHARED_FEATURE_MTL_EVENT,(WGPUFeatureName)DOE_SHARED_FEATURE_IOSURFACE};
    for(unsigned surface=0;surface<2;++surface)for(unsigned event=0;event<2;++event){
        adapter.backend.value.metal.caps.shared_surface_import=surface!=0;
        adapter.backend.value.metal.caps.timeline_enabled=event!=0;
        enumeration(&adapter,names[0],surface!=0);enumeration(&adapter,names[1],event!=0);
        WGPUDeviceDescriptor descriptor={.requiredFeatureCount=3,.requiredFeatures=names};
        uint32_t enabled=UINT32_MAX;
        bool accepted=native_requested_features(&adapter.backend,&descriptor,&enabled);
        assert(accepted==(surface&&event));
        if(!accepted){assert(enabled==UINT32_MAX);continue;}
        native_device device={.features=enabled};
        assert(wgpuDeviceHasFeature((WGPUDevice)&device,names[0]) && wgpuDeviceHasFeature((WGPUDevice)&device,names[1]));
        WGPUSupportedFeatures list={0};wgpuDeviceGetFeatures((WGPUDevice)&device,&list);
        assert(list.featureCount==3); /* Core plus exactly two distinct extensions. */
        for(size_t i=0;i<list.featureCount;++i)assert(wgpuDeviceHasFeature((WGPUDevice)&device,list.features[i]));
        wgpuSupportedFeaturesFreeMembers(list);
        assert(native_requested_features(&adapter.backend,NULL,&enabled));device.features=enabled;
        assert(!wgpuDeviceHasFeature((WGPUDevice)&device,names[0]) && !wgpuDeviceHasFeature((WGPUDevice)&device,names[1]));
    }
    const uint32_t unknown[]={0,24,25,31,32,0x50025,0x50029,0x5002B,UINT32_MAX};
    for(size_t i=0;i<sizeof(unknown)/sizeof(unknown[0]);++i){
        WGPUFeatureName name=(WGPUFeatureName)unknown[i];
        assert(!wgpuAdapterHasFeature((WGPUAdapter)&adapter,name));
        WGPUDeviceDescriptor descriptor={.requiredFeatureCount=1,.requiredFeatures=&name};
        uint32_t enabled=UINT32_MAX;assert(!native_requested_features(&adapter.backend,&descriptor,&enabled));assert(enabled==UINT32_MAX);
        native_device device={.features=UINT32_MAX};assert(!wgpuDeviceHasFeature((WGPUDevice)&device,name));
    }
    adapter.backend.kind=WGPUBackendType_Undefined;
    enumeration(&adapter,names[0],false);enumeration(&adapter,names[1],false);
    puts("PASS pinned shared feature names: queried Metal capabilities, independent admission, exact enumeration, duplicate requests, disabled devices and unknown/slot-alias rejection; host only");
}
