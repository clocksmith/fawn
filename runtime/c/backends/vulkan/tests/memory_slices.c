/* SPDX-License-Identifier: MIT
 * Native procedure-boundary allocation/retirement controls, no ICD/GPU claim. */
#include "../src/internal.h"
#include "../src/vk_abi_external.h"
#include "../src/draw_indirect_internal.h"
#include <assert.h>
#include <stdio.h>
static struct {
    struct { uint64_t size, usage, memory, offset; bool alive; } buffers[256];
    struct { unsigned char *bytes; uint64_t size; uint32_t type; } memories[256];
    struct { VkCommandBuffer command; VkBuffer buffer; uint64_t offset,size; uint32_t value; } fills[128];
    unsigned buffer_id,memory_id,allocations,frees,maps,unmaps,binds,fill_count,fill_calls;
    unsigned allocation_failures,map_failures,bind_failures;
    uint32_t bits;
    uint64_t alignment;
    bool required,preferred;
    VkResult submit_result,wait_result;
    unsigned copy_calls,copy_regions,dispatches,descriptor_updates,pools,pools_freed;
    uint32_t groups[32];
    VkDescriptorBufferInfo descriptor;
} mock;
static VkResult create_buffer(VkDevice device,const VkBufferCreateInfo *info,const VkAllocationCallbacks *a,VkBuffer *out) {
    (void)device;(void)a;
    assert(info->size && !(info->size%4) && (info->usage & DOE_VK_BUFFER_COPY_DST));
    const unsigned id=++mock.buffer_id; assert(id<256);
    mock.buffers[id].size=info->size; mock.buffers[id].usage=info->usage; mock.buffers[id].alive=true; *out=id; return 0;
}
static void destroy_buffer(VkDevice device,VkBuffer buffer,const VkAllocationCallbacks *a) {
    (void)device;(void)a; assert(mock.buffers[buffer].alive); mock.buffers[buffer].alive=false;
}
static void requirements(VkDevice device,VkBuffer buffer,VkMemoryRequirements *out) {
    (void)device;
    *out=(VkMemoryRequirements){(mock.buffers[buffer].size+mock.alignment-1u)&~(mock.alignment-1u),mock.alignment,mock.bits};
}
static void requirements2(VkDevice device,const VkBufferMemoryRequirementsInfo2 *info,VkMemoryRequirements2 *out) {
    assert(info->sType==VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 && !info->pNext);
    assert(out->sType==VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2);
    VkMemoryDedicatedRequirements *dedicated=out->pNext;
    assert(dedicated && dedicated->sType==VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
    dedicated->requiresDedicatedAllocation=mock.required; dedicated->prefersDedicatedAllocation=mock.preferred;
    requirements(device,info->buffer,&out->memoryRequirements);
}
static VkResult allocate(VkDevice device,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *a,VkDeviceMemory *out) {
    (void)device;(void)a;
    ++mock.allocations;
    if (mock.allocation_failures) { --mock.allocation_failures; return -1; }
    if (mock.required || mock.preferred) {
        const VkMemoryDedicatedAllocateInfo *dedicated=info->pNext;
        assert(dedicated && dedicated->sType==VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO &&
            mock.buffers[dedicated->buffer].alive && !dedicated->image);
    } else assert(!info->pNext);
    const unsigned id=++mock.memory_id; assert(id<256);
    mock.memories[id].bytes=malloc((size_t)info->allocationSize); assert(mock.memories[id].bytes);
    mock.memories[id].size=info->allocationSize; mock.memories[id].type=info->memoryTypeIndex;
    memset(mock.memories[id].bytes,0xa5,(size_t)info->allocationSize); *out=id; return 0;
}
static void free_memory(VkDevice device,VkDeviceMemory memory,const VkAllocationCallbacks *a) {
    (void)device;(void)a;
    for (unsigned i=1;i<=mock.buffer_id;++i) assert(!mock.buffers[i].alive || mock.buffers[i].memory!=memory);
    assert(mock.memories[memory].bytes); free(mock.memories[memory].bytes); mock.memories[memory].bytes=NULL; ++mock.frees;
}
static VkResult bind_memory(VkDevice device,VkBuffer buffer,VkDeviceMemory memory,uint64_t offset) {
    (void)device; ++mock.binds;
    if (mock.bind_failures) { --mock.bind_failures; return -1; }
    assert(!(offset%mock.alignment) && doe_vk_range(mock.memories[memory].size,offset,mock.buffers[buffer].size));
    mock.buffers[buffer].memory=memory; mock.buffers[buffer].offset=offset; return 0;
}
static VkResult map_memory(VkDevice device,VkDeviceMemory memory,uint64_t offset,uint64_t size,VkFlags flags,void **out) {
    (void)device;(void)flags; ++mock.maps;
    if (mock.map_failures) { --mock.map_failures; return -1; }
    assert(doe_vk_range(mock.memories[memory].size,offset,size)); *out=mock.memories[memory].bytes+offset; return 0;
}
static void unmap(VkDevice device,VkDeviceMemory memory) { (void)device; assert(mock.memories[memory].bytes); ++mock.unmaps; }
static void fill(VkCommandBuffer command,VkBuffer buffer,uint64_t offset,uint64_t size,uint32_t value) {
    assert(!(offset%4) && !(size%4) && doe_vk_range(mock.buffers[buffer].size,offset,size));
    assert(mock.fill_count<128);
    mock.fills[mock.fill_count].command=command; mock.fills[mock.fill_count].buffer=buffer;
    mock.fills[mock.fill_count].offset=offset; mock.fills[mock.fill_count].size=size;
    mock.fills[mock.fill_count++].value=value; ++mock.fill_calls;
}
static void barrier(VkCommandBuffer command,VkFlags source,VkFlags destination,VkFlags flags,
    uint32_t memory,const void *m,uint32_t buffers,const void *b,uint32_t images,const void *i) {
    (void)command;(void)source;(void)destination;(void)flags;(void)memory;(void)m;(void)buffers;(void)b;(void)images;(void)i;
}
static VkResult end(VkCommandBuffer command) { (void)command; return 0; }
static VkResult queue(VkQueue q,uint32_t count,const VkSubmitInfo *submits,VkFence fence) {
    (void)q;(void)fence; assert(count==1 && submits->commandBufferCount==1);
    if (!mock.submit_result) for (unsigned i=0;i<mock.fill_count;++i) {
        if (mock.fills[i].command!=submits->pCommandBuffers[0]) continue;
        const VkBuffer buffer=mock.fills[i].buffer;
        unsigned char *bytes=mock.memories[mock.buffers[buffer].memory].bytes+mock.buffers[buffer].offset+mock.fills[i].offset;
        for (uint64_t j=0;j<mock.fills[i].size;j+=4) memcpy(bytes+j,&mock.fills[i].value,4);
    }
    return mock.submit_result;
}
static VkResult wait_fences(VkDevice device,uint32_t count,const VkFence *fences,VkBool32 all,uint64_t timeout) {
    (void)device;(void)fences;(void)all;(void)timeout; assert(count==1); return mock.wait_result;
}
static void destroy_fence(VkDevice device,VkFence fence,const VkAllocationCallbacks *a) {
    (void)device;(void)a;
    unsigned out=0;
    for (unsigned i=0;i<mock.fill_count;++i) if ((uintptr_t)mock.fills[i].command!=fence) mock.fills[out++]=mock.fills[i];
    mock.fill_count=out;
}
static void copies(VkCommandBuffer command,VkBuffer source,VkBuffer destination,uint32_t count,const VkBufferCopy *regions) {
    (void)command; ++mock.copy_calls; mock.copy_regions+=count;
    for (uint32_t i=0;i<count;++i) {
        assert(doe_vk_range(mock.buffers[source].size,regions[i].srcOffset,regions[i].size));
        assert(doe_vk_range(mock.buffers[destination].size,regions[i].dstOffset,regions[i].size));
        const unsigned char *from=mock.memories[mock.buffers[source].memory].bytes+mock.buffers[source].offset+regions[i].srcOffset;
        unsigned char *to=mock.memories[mock.buffers[destination].memory].bytes+mock.buffers[destination].offset+regions[i].dstOffset;
        memcpy(to,from,(size_t)regions[i].size);
    }
}
static VkResult create_pool(VkDevice device,const VkDescriptorPoolCreateInfo *info,const VkAllocationCallbacks *a,VkDescriptorPool *out) {
    (void)device;(void)info;(void)a; *out=++mock.pools; return 0;
}
static void destroy_pool(VkDevice device,VkDescriptorPool pool,const VkAllocationCallbacks *a) {
    (void)device;(void)a; assert(pool); ++mock.pools_freed;
}
static VkResult allocate_sets(VkDevice device,const VkDescriptorSetAllocateInfo *info,VkDescriptorSet *sets) {
    (void)device; assert(info->descriptorSetCount==1); sets[0]=77; return 0;
}
static void update_sets(VkDevice device,uint32_t count,const VkWriteDescriptorSet *writes,uint32_t copied,const void *copy) {
    (void)device;(void)copy; assert(count==1 && !copied && writes->pBufferInfo);
    mock.descriptor=*writes->pBufferInfo; ++mock.descriptor_updates;
}
static void bind_pipeline(VkCommandBuffer command,int32_t point,VkPipeline pipeline) {
    (void)command; assert(point==VK_PIPELINE_BIND_POINT_COMPUTE && pipeline==9);
}
static void bind_sets(VkCommandBuffer command,int32_t point,VkPipelineLayout layout,uint32_t first,
    uint32_t count,const VkDescriptorSet *sets,uint32_t dynamic,const uint32_t *offsets) {
    (void)command;(void)layout;(void)offsets; assert(point==VK_PIPELINE_BIND_POINT_COMPUTE && !first && count==1 && *sets==77 && !dynamic);
}
static void dispatch(VkCommandBuffer command,uint32_t x,uint32_t y,uint32_t z) {
    (void)command; assert(x && y==1 && z==1 && mock.dispatches<32);
    assert(mock.descriptor.range==32u*x && !(mock.descriptor.offset%16)); mock.groups[mock.dispatches++]=x;
}
static doe_vk_context context(void) {
    doe_vk_context c={.references=1};
    c.memory.memoryTypeCount=3; c.memory.memoryHeapCount=1;
    c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    c.memory.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    c.memory.memoryTypes[2].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    c.vk=(doe_vk_functions){.vkCreateBuffer=create_buffer,.vkDestroyBuffer=destroy_buffer,
        .vkGetBufferMemoryRequirements=requirements,.vkGetBufferMemoryRequirements2=requirements2,
        .vkAllocateMemory=allocate,.vkFreeMemory=free_memory,.vkBindBufferMemory=bind_memory,
        .vkMapMemory=map_memory,.vkUnmapMemory=unmap,.vkCmdFillBuffer=fill,.vkCmdPipelineBarrier=barrier,
        .vkEndCommandBuffer=end,.vkQueueSubmit=queue,.vkWaitForFences=wait_fences,.vkDestroyFence=destroy_fence,
        .vkCmdCopyBuffer=copies,.vkCreateDescriptorPool=create_pool,.vkDestroyDescriptorPool=destroy_pool,
        .vkAllocateDescriptorSets=allocate_sets,.vkUpdateDescriptorSets=update_sets,.vkCmdBindPipeline=bind_pipeline,
        .vkCmdBindDescriptorSets=bind_sets,.vkCmdDispatch=dispatch};
    mock.bits=7; mock.alignment=256; return c;
}
static doe_vk_buffer *buffer(doe_vk_context *c,uint64_t bytes,doe_vk_memory_kind kind) {
    doe_vk_buffer *out=NULL;
    assert(doe_vk_buffer_create(c,bytes,DOE_VK_BUFFER_STORAGE|DOE_VK_BUFFER_COPY_DST,kind,&out,NULL)==DOE_VK_OK); return out;
}
static doe_vk_commands *recording(doe_vk_context *c,bool reusable) {
    doe_vk_commands *out=calloc(1,sizeof(*out)); assert(out && doe_vk_context_retain(c)==DOE_VK_OK);
    *out=(doe_vk_commands){.context=c,.references=1,.state=DOE_VK_RECORDING,.uses_active=true,.reusable=reusable};
    out->command=(void *)out; out->fence=(uintptr_t)out; return out;
}
static void close_context(doe_vk_context *c) {
    assert(c->references==1); doe_vk_memory_blocks_clear(c); assert(!c->memory_block_count);
    for (unsigned i=1;i<=mock.memory_id;++i) assert(!mock.memories[i].bytes);
}
static void placement(void) {
    doe_vk_context c=context();
    doe_vk_buffer *a=buffer(&c,257,DOE_VK_MEMORY_HOST),*b=buffer(&c,65,DOE_VK_MEMORY_HOST);
    assert(a->memory==b->memory && a->slice.offset==0 && b->slice.offset>=a->slice.size);
    assert(!(b->slice.offset%256) && c.memory_block_count==1);
    memset(a->mapped,0x7b,257); memset(b->mapped,0x5c,65);
    const uint64_t old=a->slice.offset;
    doe_vk_buffer_release(a); a=buffer(&c,257,DOE_VK_MEMORY_HOST);
    assert(a->slice.offset==old);
    for (unsigned i=0;i<257;++i) assert(((unsigned char *)a->mapped)[i]==0);
    for (unsigned i=0;i<65;++i) assert(((unsigned char *)b->mapped)[i]==0x5c);
    mock.bits=2; doe_vk_buffer *different=buffer(&c,257,DOE_VK_MEMORY_HOST);
    assert(different->memory!=a->memory && mock.memories[different->memory].type==1);
    assert(a->size==257 && a->native_size==260 && a->usage==(DOE_VK_BUFFER_STORAGE|DOE_VK_BUFFER_COPY_DST));
    doe_vk_buffer_release(a); doe_vk_buffer_release(b); doe_vk_buffer_release(different); close_context(&c);
}
static void small_native_alignment(void) {
    doe_vk_context c=context(); mock.alignment=4;
    doe_vk_buffer *a=buffer(&c,3,DOE_VK_MEMORY_HOST),*b=buffer(&c,3,DOE_VK_MEMORY_HOST);
    assert(a->size==3 && b->size==3 && a->native_size==4 && b->native_size==4);
    assert(a->memory==b->memory && b->slice.offset>=a->slice.size);
    assert(!((uintptr_t)a->mapped%_Alignof(max_align_t)) && !((uintptr_t)b->mapped%_Alignof(max_align_t)));
    doe_vk_buffer_release(a); doe_vk_buffer_release(b); close_context(&c);
}
static void exclusive_and_faults(void) {
    for (unsigned mode=0;mode<3;++mode) {
        doe_vk_context c=context(); mock.required=mode==0; mock.preferred=mode==1;
        if (mode==2) c.vk.vkGetBufferMemoryRequirements2=NULL;
        doe_vk_buffer *a=buffer(&c,257,DOE_VK_MEMORY_HOST),*b=buffer(&c,257,DOE_VK_MEMORY_HOST);
        assert(!a->slice.block && !b->slice.block && a->memory!=b->memory && !c.memory_block_count);
        doe_vk_buffer_release(a); doe_vk_buffer_release(b); close_context(&c);
    }
    mock.required=mock.preferred=false;
    for (unsigned mode=0;mode<3;++mode) {
        doe_vk_context c=context();
        mock.allocation_failures=mode==0 ? 2 : 0; mock.map_failures=mode==1 ? 2 : 0; mock.bind_failures=mode==2 ? 1 : 0;
        doe_vk_buffer *out=(void *)&c;
        assert(doe_vk_buffer_create(&c,257,DOE_VK_BUFFER_STORAGE,DOE_VK_MEMORY_HOST,&out,NULL)==DOE_VK_OUT_OF_MEMORY);
        assert(out==(void *)&c && c.references==1 && !c.memory_block_count); close_context(&c);
    }
    doe_vk_context c=context(); mock.allocation_failures=1;
    doe_vk_buffer *a=buffer(&c,257,DOE_VK_MEMORY_HOST);
    assert(!a->slice.block); doe_vk_buffer_release(a); close_context(&c);
}
static void bounded_blocks(void) {
    doe_vk_context c=context(); doe_vk_buffer *rows[17];
    for (unsigned i=0;i<17;++i) rows[i]=buffer(&c,2u*1024u*1024u,DOE_VK_MEMORY_HOST);
    assert(c.memory_block_count==8 && !rows[16]->slice.block);
    for (unsigned i=0;i<17;++i) doe_vk_buffer_release(rows[i]);
    close_context(&c);
    c=context(); doe_vk_buffer *large=buffer(&c,3u*1024u*1024u,DOE_VK_MEMORY_HOST);
    assert(!large->slice.block && !c.memory_block_count); doe_vk_buffer_release(large); close_context(&c);
}
/* The first block of a memory type is small when the request fits; full blocks
 * alone count toward the limit. */
static void small_first_block(void) {
    doe_vk_context c=context();
    doe_vk_buffer *small=buffer(&c,16,DOE_VK_MEMORY_HOST),*next=buffer(&c,64u*1024u,DOE_VK_MEMORY_HOST);
    assert(small->slice.block && mock.memories[small->memory].size==256u*1024u && next->memory==small->memory);
    doe_vk_buffer *rows[17];
    for (unsigned i=0;i<17;++i) rows[i]=buffer(&c,2u*1024u*1024u,DOE_VK_MEMORY_HOST);
    assert(c.memory_block_count==9 && rows[15]->slice.block && !rows[16]->slice.block);
    assert(mock.memories[rows[0]->memory].size==4u*1024u*1024u);
    doe_vk_buffer *spill=buffer(&c,16,DOE_VK_MEMORY_HOST); assert(spill->memory==small->memory);
    /* A type with no block yet but a request above a quarter of the small block
     * takes no small block, and the full-block limit is already reached. */
    mock.bits=2; doe_vk_buffer *wide=buffer(&c,128u*1024u,DOE_VK_MEMORY_HOST);
    assert(!wide->slice.block && c.memory_block_count==9);
    for (unsigned i=0;i<17;++i) doe_vk_buffer_release(rows[i]);
    doe_vk_buffer_release(small); doe_vk_buffer_release(next); doe_vk_buffer_release(spill); doe_vk_buffer_release(wide);
    close_context(&c);
}
static void zero_and_retirement(void) {
    doe_vk_context c=context(); mock.bits=4;
    doe_vk_buffer *a=NULL;
    assert(doe_vk_buffer_create(&c,3,DOE_VK_BUFFER_STORAGE,DOE_VK_MEMORY_DEVICE,&a,NULL)==DOE_VK_OK);
    assert(a->usage==DOE_VK_BUFFER_STORAGE && (mock.buffers[a->buffer].usage & DOE_VK_BUFFER_COPY_DST));
    assert(a->needs_zero && !a->mapped && a->native_size==4);
    doe_vk_commands *cmd=recording(&c,false),*other=recording(&c,false);
    const unsigned fills=mock.fill_calls;
    assert(doe_vk_use_buffer(cmd,a)==DOE_VK_OK && a->needs_zero && a->zero_owner==cmd && mock.fill_calls==fills+1);
    assert(doe_vk_use_buffer(other,a)==DOE_VK_BUSY);
    assert(doe_vk_commands_fill(other,a,0,4,0)==DOE_VK_INVALID_ARGUMENT);
    assert(doe_vk_commands_release(cmd)==DOE_VK_OK && a->needs_zero && !a->zero_owner);
    assert(doe_vk_commands_release(other)==DOE_VK_OK);
    cmd=recording(&c,true); assert(doe_vk_use_buffer(cmd,a)==DOE_VK_BUSY && !a->zero_owner);
    assert(doe_vk_commands_release(cmd)==DOE_VK_OK);
    cmd=recording(&c,false); assert(doe_vk_use_buffer(cmd,a)==DOE_VK_OK);
    assert(doe_vk_commands_submit(cmd,NULL)==DOE_VK_OK && !a->needs_zero && !a->zero_owner);
    const VkDeviceMemory memory=a->memory; const uint64_t offset=a->slice.offset;
    for (unsigned i=0;i<4;++i) assert(mock.memories[memory].bytes[offset+i]==0);
    assert(mock.memories[memory].bytes[offset+4]==0xa5);
    doe_vk_buffer_release(a); a=buffer(&c,3,DOE_VK_MEMORY_DEVICE);
    assert(a->memory==memory && a->slice.offset!=offset);
    mock.wait_result=2; assert(doe_vk_commands_wait(cmd,0,NULL)==DOE_VK_TIMEOUT && doe_vk_commands_release(cmd)==DOE_VK_BUSY);
    mock.wait_result=0; assert(doe_vk_commands_wait(cmd,0,NULL)==DOE_VK_OK && doe_vk_commands_release(cmd)==DOE_VK_OK);
    doe_vk_buffer *reused=buffer(&c,3,DOE_VK_MEMORY_DEVICE); assert(reused->slice.offset==offset && reused->needs_zero);
    doe_vk_buffer_release(a); doe_vk_buffer_release(reused);
    a=buffer(&c,256,DOE_VK_MEMORY_DEVICE); cmd=recording(&c,false);
    assert(doe_vk_use_buffer(cmd,a)==DOE_VK_OK); mock.submit_result=-1;
    assert(doe_vk_commands_submit(cmd,NULL)==DOE_VK_OUT_OF_MEMORY && a->needs_zero && a->zero_owner==cmd);
    const uint64_t unknown=a->slice.offset; doe_vk_buffer_release(a);
    a=buffer(&c,256,DOE_VK_MEMORY_DEVICE); assert(a->slice.offset!=unknown);
    assert(doe_vk_commands_release(cmd)==DOE_VK_BUSY);
    assert(doe_vk_commands_wait(cmd,0,NULL)==DOE_VK_OUT_OF_MEMORY && doe_vk_commands_release(cmd)==DOE_VK_OK);
    mock.submit_result=0; reused=buffer(&c,256,DOE_VK_MEMORY_DEVICE); assert(reused->slice.offset==unknown);
    doe_vk_buffer_release(a); doe_vk_buffer_release(reused);
    a=buffer(&c,256,DOE_VK_MEMORY_DEVICE); cmd=recording(&c,true);
    const unsigned before=mock.fill_calls;
    assert(doe_vk_commands_fill(cmd,a,0,256,7)==DOE_VK_OK && mock.fill_calls==before+1);
    assert(doe_vk_commands_submit(cmd,NULL)==DOE_VK_OK && !a->needs_zero);
    assert(doe_vk_commands_wait(cmd,0,NULL)==DOE_VK_OK);
    assert(doe_vk_commands_release(cmd)==DOE_VK_OK); doe_vk_buffer_release(a); close_context(&c);
}
static void batch_records(void) {
    doe_vk_context c=context(); c.caps.storage_offset_alignment=256; c.caps.max_storage_buffer_range=1024;
    c.caps.max_workgroup_count[0]=64; c.caps.draw_indirect_first_instance=true;
    doe_vk_buffer *arguments=NULL;
    assert(doe_vk_buffer_create(&c,96,DOE_VK_BUFFER_COPY_SRC|DOE_VK_BUFFER_COPY_DST|DOE_VK_BUFFER_INDIRECT,
        DOE_VK_MEMORY_HOST,&arguments,NULL)==DOE_VK_OK);
    uint32_t words[24]; for (unsigned i=0;i<24;++i) words[i]=100u+i;
    assert(doe_vk_buffer_write(arguments,0,words,sizeof(words))==DOE_VK_OK);
    doe_vk_pipeline pipeline={.context=&c,.references=1,.pipeline=9,.binding_count=1,.group_count=1};
    pipeline.bindings[0]=(doe_vk_binding_layout){0,0,DOE_VK_BINDING_STORAGE};
    doe_vk_draw_validator validator={.references=1,.batch=true,.pipeline=&pipeline};
    doe_vk_render_indirect indirect[3]={{&validator,arguments,0,false},{&validator,arguments,32,true},{&validator,arguments,64,false}};
    doe_vk_draw_indirect_request requests[3]={
        {.indirect=&indirect[0],.limits={4,8}}, {.indirect=&indirect[1],.limits={9,7},.indexed=true},
        {.indirect=&indirect[2],.limits={5,6}}};
    for (unsigned mode=0;mode<3;++mode) {
        validator.batch=mode!=1; c.caps.max_storage_buffer_range=mode==2 ? 64 : 1024;
        const unsigned before=mock.dispatches, copy_before=mock.copy_calls, regions_before=mock.copy_regions;
        doe_vk_commands *cmd=recording(&c,false);
        assert(doe_vk_draw_indirect_prepare_batch(cmd,requests,3)==DOE_VK_OK);
        assert(requests[0].buffer==requests[1].buffer && requests[1].buffer==requests[2].buffer);
        assert(requests[0].offset==0 && requests[1].offset==(mode==1 ? 256 : 32) &&
            requests[2].offset==(mode==0 ? 64 : mode==1 ? 512 : 256));
        assert(mock.copy_calls==copy_before+1 && mock.copy_regions==regions_before+3);
        assert(mock.dispatches==before+(mode==0 ? 1 : mode==1 ? 3 : 2));
        assert(mock.groups[before]==(mode==0 ? 3 : mode==1 ? 1 : 2));
        const VkBuffer scratch=requests[0].buffer;
        const unsigned char *bytes=mock.memories[mock.buffers[scratch].memory].bytes+mock.buffers[scratch].offset;
        for (unsigned i=0;i<3;++i) {
            const unsigned copied=requests[i].indexed ? 20u : 16u;
            assert(!memcmp(bytes+requests[i].offset,words+8u*i,copied));
            uint32_t tail[4]={0}; memcpy(tail,bytes+requests[i].offset+copied,32u-copied);
            assert(tail[0]==requests[i].limits[0] && tail[1]==requests[i].limits[1]);
            assert(tail[requests[i].indexed ? 2 : 3]==(requests[i].indexed ? 3u : 0u));
        }
        assert(doe_vk_commands_release(cmd)==DOE_VK_OK && arguments->references==1 && pipeline.references==1);
    }
    const unsigned allocations=mock.allocations;
    indirect[2].offset=84;
    doe_vk_commands *cmd=recording(&c,false);
    assert(doe_vk_draw_indirect_prepare_batch(cmd,requests,3)==DOE_VK_INVALID_ARGUMENT);
    assert(mock.allocations==allocations && !cmd->buffers && !cmd->dispatches);
    assert(doe_vk_commands_release(cmd)==DOE_VK_OK);
    assert(mock.pools==mock.pools_freed);
    doe_vk_buffer_release(arguments); close_context(&c);
}

static void candidate_uncached_4mb_and_chunked_wc(void) {
    doe_vk_context c=context();
    c.memory.memoryTypes[1].propertyFlags|=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    c.caps.resource_profile=DOE_VK_RESOURCE_DEVICE_LOCAL_HOST_WRITES|
        DOE_VK_RESOURCE_SUBALLOCATE_4MB|DOE_VK_RESOURCE_CHUNKED_WC_WRITE;
    const unsigned alloc_before=mock.allocations, map_before=mock.maps;
    const unsigned unmap_before=mock.unmaps, free_before=mock.frees;
    doe_vk_buffer *in_buf=buffer(&c,4u*1024u*1024u,DOE_VK_MEMORY_HOST_DEVICE_LOCAL);
    doe_vk_buffer *out_buf=buffer(&c,4u*1024u*1024u,DOE_VK_MEMORY_HOST_DEVICE_LOCAL);
    doe_vk_buffer *readback=buffer(&c,4u*1024u*1024u,DOE_VK_MEMORY_HOST);
    doe_vk_buffer *params=buffer(&c,16,DOE_VK_MEMORY_HOST_DEVICE_LOCAL);
    assert(!in_buf->host_cached && !out_buf->host_cached && !params->host_cached && readback->host_cached);
    assert(in_buf->slice.block && in_buf->slice.block==out_buf->slice.block && in_buf->slice.block==params->slice.block);
    assert(!readback->slice.block && c.memory_block_count==1);
    assert(mock.memories[in_buf->memory].size==12u*1024u*1024u);
    assert(mock.memories[readback->memory].size==4u*1024u*1024u);
    assert(mock.allocations==alloc_before+2 && mock.maps==map_before+2);

    /* Unaligned source, non-multiple tail, 1 MiB write and poisoned-content healing. */
    const size_t raw_len=1024u*1024u+64u;
    unsigned char *raw=malloc(raw_len);
    unsigned char *got=malloc(raw_len);
    assert(raw && got);
    for (size_t i=0;i<raw_len;++i) raw[i]=(unsigned char)((i*131u+17u)&0xffu);
    const unsigned char *unaligned=raw+3u;
    const size_t tail_len=65536u+37u;
    assert(doe_vk_buffer_write(in_buf,256,unaligned,tail_len)==DOE_VK_OK);
    assert(doe_vk_buffer_read(in_buf,256,got,tail_len)==DOE_VK_OK);
    assert(!memcmp(got,unaligned,tail_len));
    assert(doe_vk_buffer_write(in_buf,0,unaligned,1024u*1024u)==DOE_VK_OK);
    assert(doe_vk_buffer_read(in_buf,0,got,1024u*1024u)==DOE_VK_OK);
    assert(!memcmp(got,unaligned,1024u*1024u));
    in_buf->content_status=DOE_VK_NATIVE_FAILURE;
    assert(doe_vk_buffer_write(in_buf,0,raw,65536u)==DOE_VK_NATIVE_FAILURE);
    unsigned char *full=malloc(4u*1024u*1024u);
    assert(full);
    for (size_t i=0;i<4u*1024u*1024u;++i) full[i]=(unsigned char)((i*73u+5u)&0xffu);
    assert(doe_vk_buffer_write(in_buf,0,full,4u*1024u*1024u)==DOE_VK_OK && in_buf->content_status==DOE_VK_OK);
    assert(!memcmp(in_buf->mapped,full,4u*1024u*1024u));
    free(full); free(got); free(raw);

    doe_vk_buffer_release(params);
    doe_vk_buffer_release(out_buf);
    doe_vk_buffer_release(in_buf);
    /* All three WC buffers came from the 12 MiB block: zero unmap/free at buffer release. */
    assert(mock.unmaps==unmap_before && mock.frees==free_before);
    doe_vk_buffer_release(readback);
    assert(mock.unmaps==unmap_before+1 && mock.frees==free_before+1);
    close_context(&c);
    assert(mock.unmaps==unmap_before+2 && mock.frees==free_before+2);

    /* Uncached allocations <= 2 MiB still size new blocks at 4 MiB. */
    c=context();
    c.memory.memoryTypes[1].propertyFlags|=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    c.caps.resource_profile=DOE_VK_RESOURCE_DEVICE_LOCAL_HOST_WRITES|DOE_VK_RESOURCE_SUBALLOCATE_4MB;
    doe_vk_buffer *one_mb=buffer(&c,1024u*1024u,DOE_VK_MEMORY_HOST_DEVICE_LOCAL);
    assert(one_mb->slice.block && mock.memories[one_mb->memory].size==4u*1024u*1024u);
    doe_vk_buffer_release(one_mb);
    close_context(&c);
}

int main(void) {
    placement(); small_native_alignment(); exclusive_and_faults(); bounded_blocks(); small_first_block();
    zero_and_retirement(); batch_records();
    candidate_uncached_4mb_and_chunked_wc();
    puts("PASS memory slices: actual-type/aligned nonoverlap, small first block outside the full-block limit, zero reuse/canaries, dedicated and absent-query fallback, allocation/map/bind rollback, private padding, abandoned/accepted/unknown initialization and completion-held reuse; batched/mixed/legacy argument offsets, grouped copies, bounded dispatches and per-record metadata; native procedure controls only");
    return 0;
}
