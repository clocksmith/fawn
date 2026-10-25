/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "cache_file.h"
#include "../../../common/include/doe_sha256.h"

enum { CACHE_ENVELOPE_SIZE = 96, CACHE_HEADER_SIZE = 32 };
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p+4)<<32; }
static void store32(uint8_t *p, uint32_t value) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(8*i));
}
static void store64(uint8_t *p, uint64_t value) { store32(p,(uint32_t)value); store32(p+4,(uint32_t)(value>>32)); }
static bool compatible(const doe_vk_context *context, const uint8_t *bytes, size_t size) {
    if (size < CACHE_ENVELOPE_SIZE + CACHE_HEADER_SIZE || memcmp(bytes,"DOEPC001",8) ||
        memcmp(bytes+8,context->caps.identity.device_uuid,16) || memcmp(bytes+24,context->caps.identity.driver_uuid,16) ||
        u32(bytes+40) != context->properties.driverVersion || u32(bytes+44) || u64(bytes+56) ||
        u64(bytes+48) != size-CACHE_ENVELOPE_SIZE) return false;
    uint8_t digest[32]; doe_common_sha256(bytes+CACHE_ENVELOPE_SIZE,size-CACHE_ENVELOPE_SIZE,digest);
    if (memcmp(digest,bytes+64,32)) return false;
    const uint8_t *header = bytes+CACHE_ENVELOPE_SIZE;
    return u32(header) >= CACHE_HEADER_SIZE && u32(header) <= size-CACHE_ENVELOPE_SIZE && u32(header+4) == 1 &&
        u32(header+8) == context->properties.vendorID && u32(header+12) == context->properties.deviceID &&
        !memcmp(header+16,context->properties.pipelineCacheUUID,16);
}
static doe_vk_status load_functions(doe_vk_context *context, doe_vk_error *error) {
    struct symbol { const char *name; void *output; size_t size; } symbols[] = {
        {"vkCreatePipelineCache",&context->vk.vkCreatePipelineCache,sizeof(context->vk.vkCreatePipelineCache)},
        {"vkDestroyPipelineCache",&context->vk.vkDestroyPipelineCache,sizeof(context->vk.vkDestroyPipelineCache)},
        {"vkGetPipelineCacheData",&context->vk.vkGetPipelineCacheData,sizeof(context->vk.vkGetPipelineCacheData)}
    };
    if (context->vk.vkCreatePipelineCache && context->vk.vkDestroyPipelineCache && context->vk.vkGetPipelineCacheData)
        return DOE_VK_OK;
    for (size_t i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
        doe_vk_status status=doe_vk_load_symbol(context->library,symbols[i].name,symbols[i].output,symbols[i].size,error);
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}
void doe_vk_driver_cache_clear(doe_vk_context *context) {
    if (context->driver_cache) context->vk.vkDestroyPipelineCache(context->device,context->driver_cache,NULL);
    context->driver_cache=0; free(context->driver_cache_path); context->driver_cache_path=NULL;
    context->driver_cache_stats.enabled=false; context->driver_cache_stats.dirty=false;
}
void doe_vk_driver_cache_record(doe_vk_context *context, bool success) {
    if (!context->driver_cache) return;
    if (success) { ++context->driver_cache_stats.additions; context->driver_cache_stats.dirty=true; }
    else ++context->driver_cache_stats.addition_failures;
}
doe_vk_status doe_vk_context_pipeline_cache(doe_vk_context *context, const char *directory,
    size_t length, doe_vk_error *error) {
    if (!context || (!directory && length) || (directory && (!length || length>16384 || memchr(directory,0,length))))
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT,"pipeline cache directory",error);
    if (!directory) { doe_vk_driver_cache_clear(context); return DOE_VK_OK; }
    if (!doe_vk_context_alive(context)) return doe_vk_fail(DOE_VK_INVALID_STATE,"pipeline cache device",error);
    doe_vk_status status=load_functions(context,error); if (status != DOE_VK_OK) return status;
    const char suffix[]="/doe-vulkan-v1.cache";
    char *path=malloc(length+sizeof(suffix)); if (!path) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY,"pipeline cache path",error);
    memcpy(path,directory,length); path[length]=0;
    if (!doe_vk_cache_directory(path)) { free(path); return doe_vk_fail(DOE_VK_NATIVE_FAILURE,"pipeline cache directory creation",error); }
    memcpy(path+length,suffix,sizeof(suffix));
    uint8_t *bytes=NULL; size_t size=0;
    ++context->driver_cache_stats.load_attempts;
    bool initial=doe_vk_cache_read(path,&bytes,&size) && compatible(context,bytes,size);
    VkPipelineCacheCreateInfo create={.sType=17,.initialDataSize=initial ? size-CACHE_ENVELOPE_SIZE : 0,
        .pInitialData=initial ? bytes+CACHE_ENVELOPE_SIZE : NULL};
    VkPipelineCache cache=0;
    VkResult result=context->vk.vkCreatePipelineCache(context->device,&create,NULL,&cache);
    if ((result == -13 || result == -9) && initial) {
        create.initialDataSize=0; create.pInitialData=NULL; initial=false;
        result=context->vk.vkCreatePipelineCache(context->device,&create,NULL,&cache);
    }
    free(bytes);
    status=doe_vk_result(context,result,"vkCreatePipelineCache",error);
    if (status != DOE_VK_OK) { free(path); return status; }
    doe_vk_driver_cache_clear(context);
    context->driver_cache=cache; context->driver_cache_path=path; context->driver_cache_stats.enabled=true;
    if (initial) ++context->driver_cache_stats.load_successes;
    return DOE_VK_OK;
}
doe_vk_status doe_vk_context_pipeline_cache_stats(const doe_vk_context *context, doe_vk_pipeline_cache_stats *out) {
    if (!context || !out) return DOE_VK_INVALID_ARGUMENT;
    *out=context->driver_cache_stats; return DOE_VK_OK;
}
doe_vk_status doe_vk_context_pipeline_cache_flush(doe_vk_context *context, doe_vk_error *error) {
    if (!context) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT,"pipeline cache flush",error);
    if (!context->driver_cache || !context->driver_cache_stats.dirty) return DOE_VK_OK;
    size_t size=0;
    VkResult result=context->vk.vkGetPipelineCacheData(context->device,context->driver_cache,&size,NULL);
    doe_vk_status status=doe_vk_result(context,result,"vkGetPipelineCacheData(size)",error);
    uint8_t *bytes=NULL;
    if (status == DOE_VK_OK && (size<CACHE_HEADER_SIZE || size>DOE_VK_CACHE_MAX_BYTES-CACHE_ENVELOPE_SIZE))
        status=doe_vk_fail(DOE_VK_UNSUPPORTED,"pipeline cache blob size",error);
    size_t capacity=size;
    if (status == DOE_VK_OK) {
        bytes=calloc(1,size+CACHE_ENVELOPE_SIZE);
        if (!bytes) status=doe_vk_fail(DOE_VK_OUT_OF_MEMORY,"pipeline cache blob",error);
    }
    if (status == DOE_VK_OK) {
        result=context->vk.vkGetPipelineCacheData(context->device,context->driver_cache,&size,bytes+CACHE_ENVELOPE_SIZE);
        status=doe_vk_result(context,result,"vkGetPipelineCacheData(bytes)",error);
        if (status == DOE_VK_OK && size>capacity) status=doe_vk_fail(DOE_VK_NATIVE_FAILURE,"pipeline cache size changed",error);
    }
    if (status == DOE_VK_OK) {
        memcpy(bytes,"DOEPC001",8); memcpy(bytes+8,context->caps.identity.device_uuid,16);
        memcpy(bytes+24,context->caps.identity.driver_uuid,16); store32(bytes+40,context->properties.driverVersion);
        store64(bytes+48,size);
        doe_common_sha256(bytes+CACHE_ENVELOPE_SIZE,size,bytes+64);
        if (!compatible(context,bytes,size+CACHE_ENVELOPE_SIZE)) status=doe_vk_fail(DOE_VK_NATIVE_FAILURE,"pipeline cache driver header",error);
        else if (!doe_vk_cache_replace(context->driver_cache_path,bytes,size+CACHE_ENVELOPE_SIZE))
            status=doe_vk_fail(DOE_VK_NATIVE_FAILURE,"pipeline cache atomic publication",error);
    }
    free(bytes);
    if (status == DOE_VK_OK) { ++context->driver_cache_stats.serializations; context->driver_cache_stats.dirty=false; }
    else ++context->driver_cache_stats.serialization_failures;
    return status;
}
