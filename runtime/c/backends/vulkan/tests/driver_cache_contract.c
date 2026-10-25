/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../src/internal.h"
#include "../src/cache_file.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
static uint8_t blob[48] = {32,0,0,0,1,0,0,0,42,0,0,0,7,0,0,0};
static unsigned creates, loads, destroys;
static VkResult fetch_result;
static VkResult create(VkDevice device, const VkPipelineCacheCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineCache *out) {
    (void)device; (void)allocator; assert(info->sType==17 && !info->flags);
    if (info->initialDataSize) { assert(info->initialDataSize==sizeof(blob) && !memcmp(info->pInitialData,blob,sizeof(blob))); ++loads; }
    *out=++creates; return 0;
}
static void destroy(VkDevice device, VkPipelineCache cache, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator; assert(cache); ++destroys;
}
static VkResult data(VkDevice device, VkPipelineCache cache, size_t *size, void *out) {
    (void)device; assert(cache);
    if (fetch_result) return fetch_result;
    if (out) { assert(*size>=sizeof(blob)); memcpy(out,blob,sizeof(blob)); }
    *size=sizeof(blob); return 0;
}
int main(void) {
    char directory[]="/tmp/doe-driver-cache-XXXXXX"; assert(mkdtemp(directory));
    char path[128]; snprintf(path,sizeof(path),"%s/doe-vulkan-v1.cache",directory);
    doe_vk_context context={.device=(VkDevice)(uintptr_t)1};
    context.properties.vendorID=42; context.properties.deviceID=7; context.properties.driverVersion=9;
    context.vk.vkCreatePipelineCache=create; context.vk.vkDestroyPipelineCache=destroy; context.vk.vkGetPipelineCacheData=data;
    doe_vk_error error={0};
    assert(doe_vk_context_pipeline_cache(&context,directory,strlen(directory),&error)==DOE_VK_OK);
    assert(creates==1 && !loads && context.driver_cache_stats.enabled);
    doe_vk_driver_cache_record(&context,true);
    assert(doe_vk_context_pipeline_cache_flush(&context,&error)==DOE_VK_OK && !context.driver_cache_stats.dirty);
    uint8_t *saved=NULL;size_t saved_size=0;assert(doe_vk_cache_read(path,&saved,&saved_size));
    assert(saved_size==96+sizeof(blob));
    assert(doe_vk_context_pipeline_cache(&context,directory,strlen(directory),&error)==DOE_VK_OK && loads==1 && destroys==1);
    doe_vk_driver_cache_record(&context,true); fetch_result=-1;
    assert(doe_vk_context_pipeline_cache_flush(&context,&error)==DOE_VK_OUT_OF_MEMORY && error.native_result==-1);
    assert(context.driver_cache_stats.dirty && context.driver_cache_stats.serialization_failures==1);
    uint8_t *after=NULL;size_t after_size=0;assert(doe_vk_cache_read(path,&after,&after_size));
    assert(after_size==saved_size && !memcmp(saved,after,saved_size));free(after);
    fetch_result=0; saved[saved_size-1]^=1; assert(doe_vk_cache_replace(path,saved,saved_size));
    assert(doe_vk_context_pipeline_cache(&context,directory,strlen(directory),&error)==DOE_VK_OK && loads==1);
    saved[saved_size-1]^=1; assert(doe_vk_cache_replace(path,saved,saved_size));
    context.caps.identity.device_uuid[0]=1;
    assert(doe_vk_context_pipeline_cache(&context,directory,strlen(directory),&error)==DOE_VK_OK && loads==1);
    assert(doe_vk_context_pipeline_cache(&context,NULL,0,&error)==DOE_VK_OK && !context.driver_cache_stats.enabled);
    assert(creates==destroys);free(saved);assert(remove(path)==0 && rmdir(directory)==0);
    puts("PASS real bounded cache-file atomic publication, digest/UUID rejection, driver load, failure preserves prior bytes and owned cache replacement; typed native calls only");
}
