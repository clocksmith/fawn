/* SPDX-License-Identifier: MIT */
#include "execute.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    DoeNativeDevicePolicy p={0};char e[256];WGPUBackendType backend=WGPUBackendType_Undefined;
    assert(doe_runtime_load_device_policy("vulkan_doe_release",&p,e,sizeof(e),NULL,&backend));
    assert(backend==WGPUBackendType_Vulkan&&p.queue_selection==1&&p.synchronization==1&&p.upload_policy==1&&p.subgroup_policy==3);
    assert(doe_runtime_load_device_policy("vulkan-doe-compute-only-fence-diagnostic",&p,e,sizeof(e),NULL,&backend));
    assert(p.queue_selection==2&&p.synchronization==0);
    const char *metal[]={"metal_doe_app","metal_doe_directional","metal_doe_comparable","metal_doe_release"};
    const unsigned uploads[]={0,2,1,1};
    for(unsigned i=0;i<4;++i){
        assert(doe_runtime_load_device_policy(metal[i],&p,e,sizeof(e),NULL,&backend));
        assert(backend==WGPUBackendType_Metal&&p.queue_selection==1&&p.synchronization==1&&p.upload_policy==uploads[i]&&!p.subgroup_policy);
    }
    const char *d3d12[]={"d3d12_doe_app","d3d12_doe_directional","d3d12_doe_comparable","d3d12_doe_release"};
    for(unsigned i=0;i<4;++i){
        assert(doe_runtime_load_device_policy(d3d12[i],&p,e,sizeof(e),NULL,&backend));
        assert(backend==WGPUBackendType_D3D12&&p.queue_selection==1&&p.synchronization==1&&
            p.upload_policy==(i<2?0u:1u)&&!p.subgroup_policy);
    }
    assert(!doe_runtime_load_device_policy("d3d12_dawn_release",&p,e,sizeof(e),NULL,&backend));
    assert(!doe_runtime_load_device_policy("missing",&p,e,sizeof(e),NULL,&backend));
    puts("PASS canonical lane exact backend/queue/sync/upload/subgroup mappings and unsupported backend/unknown rejection");
}
