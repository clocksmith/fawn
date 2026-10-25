/* SPDX-License-Identifier: MIT */
#include "execute.h"
#include <assert.h>
#include <stdio.h>
int main(void){DoeNativeDevicePolicy p={0};char e[256];
assert(doe_runtime_load_device_policy("vulkan_doe_release",&p,e,sizeof(e),NULL));
assert(p.queue_selection==1&&p.synchronization==1&&p.upload_policy==1&&p.subgroup_policy==3);
assert(doe_runtime_load_device_policy("vulkan-doe-compute-only-fence-diagnostic",&p,e,sizeof(e),NULL));
assert(p.queue_selection==2&&p.synchronization==0);
assert(!doe_runtime_load_device_policy("metal_doe_app",&p,e,sizeof(e),NULL));
assert(!doe_runtime_load_device_policy("missing",&p,e,sizeof(e),NULL));
puts("PASS canonical lane exact queue/sync/upload/subgroup mappings and unsupported backend/unknown rejection");}
