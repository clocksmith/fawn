/* SPDX-License-Identifier: MIT
 * Context-owned Vulkan buffer allocations; buffer owners carry aligned slice
 * leases until their existing command references prove the resources unused. */
#include "internal.h"
#include "vk_abi_external.h"
/* A memory type's first block is the small one when the first request fits a
 * quarter of it, so a process that needs only a few small buffers of a type
 * does not pay the driver for a full block. At most one small block exists per
 * type (it is created only while the type has none); full blocks alone count
 * toward the limit. */
enum { MEMORY_BLOCK_BYTES=4*1024*1024, MEMORY_BLOCK_LIMIT=8, MEMORY_FIRST_BLOCK_BYTES=256*1024 };
struct doe_vk_memory_block {
    VkDeviceMemory memory;
    void *mapped;
    uint64_t capacity;
    uint32_t type;
    doe_vk_memory_slice *slices;
    struct doe_vk_memory_block *next;
};
static bool place(struct doe_vk_memory_block *block, const VkMemoryRequirements *r, doe_vk_memory_slice *slice) {
    uint64_t offset=0;
    doe_vk_memory_slice **link=&block->slices;
    for (;;) {
        if (offset>UINT64_MAX-(r->alignment-1u)) return false;
        offset=(offset+r->alignment-1u)&~(r->alignment-1u);
        const uint64_t end=*link ? (*link)->offset : block->capacity;
        if (offset<=end && r->size<=end-offset) {
            *slice=(doe_vk_memory_slice){block,*link,offset,r->size};
            *link=slice;
            return true;
        }
        if (!*link) return false;
        offset=(*link)->offset+(*link)->size;
        link=&(*link)->next;
    }
}
static doe_vk_status new_block(doe_vk_context *context,uint32_t index,uint64_t bytes,
    struct doe_vk_memory_block **out,doe_vk_error *error) {
    struct doe_vk_memory_block *block=calloc(1,sizeof(*block));
    if (!block) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY,"memory block owner",error);
    const VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=bytes,.memoryTypeIndex=index};
    doe_vk_status status=doe_vk_result(context,context->vk.vkAllocateMemory(context->device,&allocate,NULL,&block->memory),
        "vkAllocateMemory buffer block",error);
    const uint32_t visible=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (status==DOE_VK_OK && (context->memory.memoryTypes[index].propertyFlags & visible)==visible) {
        status=doe_vk_result(context,context->vk.vkMapMemory(context->device,block->memory,0,bytes,0,&block->mapped),
            "vkMapMemory buffer block",error);
        if (status==DOE_VK_OK && !block->mapped) status=doe_vk_fail(DOE_VK_NATIVE_FAILURE,"null mapped memory block",error);
    }
    if (status!=DOE_VK_OK) {
        if (block->mapped) context->vk.vkUnmapMemory(context->device,block->memory);
        if (block->memory) context->vk.vkFreeMemory(context->device,block->memory,NULL);
        free(block); return status;
    }
    block->capacity=bytes; block->type=index; block->next=context->memory_blocks;
    context->memory_blocks=block; ++context->memory_block_count; *out=block;
    return DOE_VK_OK;
}
static doe_vk_status exclusive(doe_vk_buffer *buffer,const VkMemoryRequirements *r,uint32_t index,
    bool host,bool dedicated,doe_vk_error *error) {
    doe_vk_context *context=buffer->context;
    const VkMemoryDedicatedAllocateInfo identity={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.buffer=buffer->buffer};
    const VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext=dedicated ? &identity : NULL,.allocationSize=r->size,.memoryTypeIndex=index};
    doe_vk_status status=doe_vk_result(context,context->vk.vkAllocateMemory(context->device,&allocate,NULL,&buffer->memory),
        "vkAllocateMemory buffer",error);
    if (status==DOE_VK_OK) status=doe_vk_result(context,context->vk.vkBindBufferMemory(context->device,buffer->buffer,buffer->memory,0),
        "vkBindBufferMemory",error);
    if (status==DOE_VK_OK) buffer->memory_bound=true;
    if (status==DOE_VK_OK && host) {
        status=doe_vk_result(context,context->vk.vkMapMemory(context->device,buffer->memory,0,buffer->size,0,&buffer->mapped),
            "vkMapMemory",error);
        if (status==DOE_VK_OK && !buffer->mapped) status=doe_vk_fail(DOE_VK_NATIVE_FAILURE,"null mapped buffer",error);
        if (status==DOE_VK_OK) memset(buffer->mapped,0,(size_t)buffer->size);
    }
    return status;
}
doe_vk_status doe_vk_buffer_allocate(doe_vk_buffer *buffer,const VkMemoryRequirements *r,uint32_t index,
    bool host,bool queried,bool dedicated,doe_vk_error *error) {
    doe_vk_context *context=buffer->context;
    const uint64_t maximum=context->caps.max_memory_allocation_size;
    const uint32_t heap=context->memory.memoryTypes[index].heapIndex;
    const uint64_t heap_bytes=context->memory.memoryHeaps[heap].size;
    const bool large_uncached=(context->caps.resource_profile & DOE_VK_RESOURCE_SUBALLOCATE_4MB)!=0 &&
        !(context->memory.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    const uint64_t slice_limit=large_uncached ? (uint64_t)MEMORY_BLOCK_BYTES : (uint64_t)MEMORY_BLOCK_BYTES/2u;
    const uint64_t block_bytes=r->size>(uint64_t)MEMORY_BLOCK_BYTES/2u ?
        3u*(uint64_t)MEMORY_BLOCK_BYTES : (uint64_t)MEMORY_BLOCK_BYTES;
    if (!queried || dedicated || r->size>slice_limit || r->alignment>block_bytes ||
        (maximum && maximum<block_bytes) || (heap_bytes && heap_bytes<block_bytes))
        return exclusive(buffer,r,index,host,dedicated,error);
    if (!r->alignment || (r->alignment & (r->alignment-1u)))
        return doe_vk_fail(DOE_VK_NATIVE_FAILURE,"buffer memory alignment",error);
    VkMemoryRequirements aligned=*r;
    if (aligned.alignment<_Alignof(max_align_t)) aligned.alignment=_Alignof(max_align_t);
    struct doe_vk_memory_block *block=NULL;
    bool typed=false;
    uint32_t full=0;
    for (struct doe_vk_memory_block *candidate=context->memory_blocks;candidate;candidate=candidate->next) {
        if (candidate->capacity>=MEMORY_BLOCK_BYTES) ++full;
        if (candidate->type!=index) continue;
        typed=true;
        if (!block && place(candidate,&aligned,&buffer->slice)) block=candidate;
    }
    const bool first=!typed && aligned.size<=MEMORY_FIRST_BLOCK_BYTES/4u &&
        aligned.alignment<=MEMORY_FIRST_BLOCK_BYTES/4u;
    if (!block && (first || full<MEMORY_BLOCK_LIMIT)) {
        doe_vk_status status=new_block(context,index,first ? MEMORY_FIRST_BLOCK_BYTES : block_bytes,&block,error);
        if (status==DOE_VK_OUT_OF_MEMORY) return exclusive(buffer,r,index,host,false,error);
        if (status!=DOE_VK_OK) return status;
        if (!place(block,&aligned,&buffer->slice)) abort();
    }
    if (!block) return exclusive(buffer,r,index,host,false,error);
    buffer->memory=block->memory;
    doe_vk_status status=doe_vk_result(context,context->vk.vkBindBufferMemory(context->device,buffer->buffer,block->memory,buffer->slice.offset),
        "vkBindBufferMemory slice",error);
    if (status!=DOE_VK_OK) return status;
    buffer->memory_bound=true;
    if (block->mapped) {
        void *mapped=(unsigned char *)block->mapped+(size_t)buffer->slice.offset;
        memset(mapped,0,(size_t)buffer->slice.size);
        if (host) buffer->mapped=mapped;
    } else buffer->needs_zero=true;
    return DOE_VK_OK;
}
void doe_vk_buffer_memory_release(doe_vk_buffer *buffer) {
    doe_vk_context *context=buffer->context;
    if (buffer->slice.block) {
        doe_vk_memory_slice **link=&buffer->slice.block->slices;
        while (*link && *link!=&buffer->slice) link=&(*link)->next;
        if (!*link) abort();
        *link=buffer->slice.next;
        struct doe_vk_memory_block *block=buffer->slice.block;
        if (!buffer->memory_bound && !block->slices) {
            struct doe_vk_memory_block **owner=&context->memory_blocks;
            while (*owner && *owner!=block) owner=&(*owner)->next;
            if (!*owner) abort();
            *owner=block->next;
            if (block->mapped) context->vk.vkUnmapMemory(context->device,block->memory);
            context->vk.vkFreeMemory(context->device,block->memory,NULL);
            free(block); --context->memory_block_count;
        }
        buffer->slice.block=NULL;
    } else {
        if (buffer->mapped) context->vk.vkUnmapMemory(context->device,buffer->memory);
        if (buffer->memory) context->vk.vkFreeMemory(context->device,buffer->memory,NULL);
    }
}
void doe_vk_memory_blocks_clear(doe_vk_context *context) {
    while (context->memory_blocks) {
        struct doe_vk_memory_block *block=context->memory_blocks;
        if (block->slices) abort();
        context->memory_blocks=block->next;
        if (block->mapped) context->vk.vkUnmapMemory(context->device,block->memory);
        context->vk.vkFreeMemory(context->device,block->memory,NULL);
        free(block); --context->memory_block_count;
    }
}
