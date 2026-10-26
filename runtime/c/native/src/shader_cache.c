/* SPDX-License-Identifier: MIT */
#include "shader_cache.h"

/* At most 16 MiB of direct cached allocations per device. Specialized artifacts
 * may retain an independently bounded module; no device reference cycle exists. */
static const size_t cache_entry_limit = 1024u * 1024u;

bool native_shader_cache_admits(const doe_shader *shader)
{
    return shader && doe_shader_owned_bytes(shader) <= cache_entry_limit;
}

doe_shader *native_shader_cache_module(native_shader_cache *cache, const doe_shader_options *options,
    const char *source, size_t size)
{
    if (!cache || !options) return NULL;
    for (size_t i = 0; i < NATIVE_SHADER_CACHE_SLOTS; ++i) {
        doe_shader *entry = cache->entries[i];
        if (doe_shader_module_matches(entry,options->target,options->semantic_flags,source,size,
                options->compiler_identity,options->compiler_identity_size) && doe_shader_retain(entry) == DOE_SHADER_OK)
            return entry;
    }
    return NULL;
}

doe_shader *native_shader_cache_stage(native_shader_cache *cache, const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline)
{
    if (!cache || !native_shader_cache_admits(module)) return NULL;
    for (size_t i = 0; i < NATIVE_SHADER_CACHE_SLOTS; ++i) {
        doe_shader *entry = cache->entries[i];
        bool matches = pipeline ? doe_shader_metal_pipeline_matches(entry,module,selection,values,count,pipeline) :
            doe_shader_specialization_matches(entry,module,selection,values,count);
        if (matches && doe_shader_retain(entry) == DOE_SHADER_OK) return entry;
    }
    return NULL;
}

doe_shader *native_shader_cache_insert(native_shader_cache *cache, doe_shader *shader)
{
    if (!cache || !native_shader_cache_admits(shader)) return NULL;
    for (size_t i = 0; i < NATIVE_SHADER_CACHE_SLOTS; ++i)
        if (cache->entries[i] == shader) return NULL;
    if (doe_shader_retain(shader) != DOE_SHADER_OK) return NULL;
    doe_shader *old = cache->entries[cache->next];
    cache->entries[cache->next] = shader;
    cache->next = (cache->next + 1) % NATIVE_SHADER_CACHE_SLOTS;
    return old;
}

void native_shader_cache_clear(native_shader_cache *cache)
{
    if (!cache) return;
    for (size_t i = 0; i < NATIVE_SHADER_CACHE_SLOTS; ++i) {
        doe_shader_release(cache->entries[i]);
        cache->entries[i] = NULL;
    }
    cache->next = 0;
}
