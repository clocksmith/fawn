/* SPDX-License-Identifier: MIT */
#include "shader_disk_cache.h"
#include "../../common/include/doe_sha256.h"
#include "../../backends/vulkan/src/cache_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reuse the existing bounded cross-platform cache-file owner (atomic replace,
 * UTF-8 Windows paths, bounded reads); do not introduce another persistence
 * engine. Native driver pipeline data and compiler artifacts remain separate. */
static char *cache_path(const uint8_t image_identity[32], const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline, bool create)
{
    uint8_t key[32], combined[64];
    if (!image_identity || !doe_shader_artifact_key(module,selection,values,count,pipeline,key)) return NULL;
    memcpy(combined,image_identity,32); memcpy(combined+32,key,32);
    doe_common_sha256(combined,sizeof(combined),key);
    char hex[65]; doe_common_sha256_hex(key,hex);
    const char *override = getenv("DOE_SHADER_CACHE_DIR");
    const char *home = getenv("HOME");
    const char *root = override ? override : home && *home ? home : "cache";
    if (!*root) return NULL; /* Explicit empty override disables persistence. */
    const char *suffix = override ? "" : home && *home ? "/.cache/doe/c_shader_translation_cache_v1" : "/doe/c_shader_translation_cache_v1";
    size_t length = strlen(root), extra = strlen(suffix);
    if (length > 32768 || extra > SIZE_MAX-length-1) return NULL;
    size_t directory_size = length+extra;
    char *path = malloc(directory_size+1+64+5);
    if (!path) return NULL;
    memcpy(path,root,length); memcpy(path+length,suffix,extra+1);
    if (create && !doe_vk_cache_directory(path)) { free(path); return NULL; }
    memcpy(path+directory_size,"/",1); memcpy(path+directory_size+1,hex,64);
    memcpy(path+directory_size+65,".bin",5);
    return path;
}

doe_shader *native_shader_disk_lookup(const uint8_t image_identity[32], const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline)
{
    char *path = cache_path(image_identity,module,selection,values,count,pipeline,false);
    if (!path) return NULL;
    uint8_t *bytes = NULL; size_t size = 0;
    doe_shader *shader = NULL;
    if (doe_vk_cache_read(path,&bytes,&size))
        (void)doe_shader_artifact_read(module,selection,values,count,pipeline,bytes,size,&shader);
    free(bytes); free(path);
    return shader;
}

void native_shader_disk_store(const uint8_t image_identity[32], const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline, const doe_shader *shader)
{
    size_t size = doe_shader_artifact_size(shader);
    if (!size) return;
    /* Refuse mismatched caller metadata even if the artifact itself is valid. */
    bool matches = pipeline ? doe_shader_metal_pipeline_matches(shader,module,selection,values,count,pipeline) :
        doe_shader_specialization_matches(shader,module,selection,values,count);
    if (!matches) return;
    uint8_t *bytes = malloc(size);
    if (!bytes) return;
    if (doe_shader_artifact_write(shader,bytes,size)) {
        char *path = cache_path(image_identity,module,selection,values,count,pipeline,true);
        if (path) (void)doe_vk_cache_replace(path,bytes,size);
        free(path);
    }
    free(bytes);
}
