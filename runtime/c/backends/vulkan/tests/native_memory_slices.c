/* SPDX-License-Identifier: MIT
 * Explicit CPU ICD only: actual Vulkan slice reuse, zero bytes and queue leases. */
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>
static void require(doe_vk_status status,const char *operation) {
    if (status==DOE_VK_OK) return;
    fprintf(stderr,"%s: %s\n",operation,doe_vk_status_name(status)); exit(1);
}
static doe_vk_buffer *create(doe_vk_context *context,uint64_t size,doe_vk_memory_kind kind) {
    doe_vk_buffer *out=NULL;
    require(doe_vk_buffer_create(context,size,DOE_VK_BUFFER_COPY_SRC|DOE_VK_BUFFER_COPY_DST,kind,&out,NULL),"create");
    return out;
}
static void submit_wait(doe_vk_commands *commands) {
    require(doe_vk_commands_submit(commands,NULL),"submit");
    require(doe_vk_commands_wait(commands,UINT64_C(5000000000),NULL),"wait");
}
int main(int argc,char **argv) {
    if (argc!=2 || strcmp(argv[1],"--allow-software")) return 2;
    const doe_vk_options options={.allow_cpu_adapter=true,.adapter_selection=DOE_VK_SELECT_SOFTWARE};
    doe_vk_context *context=NULL; doe_vk_error error={0};
    const doe_vk_status opened=doe_vk_context_create(&options,&context,&error);
    if (opened!=DOE_VK_OK) {
        fprintf(stderr,"CPU Vulkan allocator: %s at %s native=%d\n",doe_vk_status_name(opened),error.operation,error.native_result);
        return 1;
    }
    assert(context->caps.software_adapter && context->vk.vkGetBufferMemoryRequirements2);
    doe_vk_buffer *a=create(context,257,DOE_VK_MEMORY_HOST),*b=create(context,65,DOE_VK_MEMORY_HOST);
    assert(a->slice.block && a->memory==b->memory && b->slice.offset>=a->slice.offset+a->slice.size);
    unsigned char old[257],canary[65],read[257]; memset(old,0x7b,sizeof(old)); memset(canary,0x5c,sizeof(canary));
    require(doe_vk_buffer_write(a,0,old,sizeof(old)),"write old contents");
    require(doe_vk_buffer_write(b,0,canary,sizeof(canary)),"write neighboring canary");
    const VkDeviceMemory memory=a->memory; const uint64_t offset=a->slice.offset;
    doe_vk_buffer_release(a); a=create(context,257,DOE_VK_MEMORY_HOST);
    assert(a->memory==memory && a->slice.offset==offset && !((uintptr_t)a->mapped%_Alignof(max_align_t)));
    require(doe_vk_buffer_read(a,0,read,sizeof(read)),"read reused host slice");
    for (size_t i=0;i<sizeof(read);++i) assert(!read[i]);
    require(doe_vk_buffer_read(b,0,read,sizeof(canary)),"read neighboring slice");
    assert(!memcmp(read,canary,sizeof(canary)));
    doe_vk_buffer *device=create(context,256,DOE_VK_MEMORY_DEVICE),*readback=create(context,256,DOE_VK_MEMORY_HOST);
    assert(device->slice.block && !device->mapped);
    doe_vk_commands *commands=NULL;
    require(doe_vk_commands_create(context,&commands,NULL),"fill recording");
    require(doe_vk_commands_fill(commands,device,0,256,UINT32_C(0xdeadbeef)),"old device contents");
    submit_wait(commands); require(doe_vk_commands_release(commands),"release fill");
    const VkDeviceMemory device_memory=device->memory; const uint64_t device_offset=device->slice.offset;
    doe_vk_buffer_release(device); device=create(context,256,DOE_VK_MEMORY_DEVICE);
    assert(device->memory==device_memory && device->slice.offset==device_offset);
    const bool deferred=device->needs_zero;
    require(doe_vk_commands_create(context,&commands,NULL),"zero-copy recording");
    require(doe_vk_commands_copy(commands,device,0,readback,0,256),"copy reused device slice");
    require(doe_vk_commands_submit(commands,NULL),"submit zero copy");
    doe_vk_buffer_release(device);
    doe_vk_buffer *held=create(context,256,DOE_VK_MEMORY_DEVICE);
    assert(held->memory!=device_memory || held->slice.offset!=device_offset);
    require(doe_vk_commands_wait(commands,UINT64_C(5000000000),NULL),"complete zero copy");
    require(doe_vk_commands_release(commands),"release zero copy");
    uint32_t zeros[64]; require(doe_vk_buffer_read(readback,0,zeros,sizeof(zeros)),"read actual queue result");
    for (unsigned i=0;i<64;++i) assert(!zeros[i]);
    device=create(context,256,DOE_VK_MEMORY_DEVICE);
    assert(device->memory==device_memory && device->slice.offset==device_offset);
    printf("PASS CPU Vulkan allocator adapter=%s: actual shared memory/offsets, coherent canaries, reused device zero through %s, early-release slice held to completion; no physical claim\n",
        context->caps.device_name,deferred ? "command initialization" : "coherent initialization");
    doe_vk_buffer_release(device); doe_vk_buffer_release(held); doe_vk_buffer_release(readback);
    doe_vk_buffer_release(a); doe_vk_buffer_release(b); doe_vk_context_release(context);
    return 0;
}
