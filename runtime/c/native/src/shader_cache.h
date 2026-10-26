/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_SHADER_CACHE_H
#define DOE_NATIVE_SHADER_CACHE_H
#include "doe_shader.h"

enum { NATIVE_SHADER_CACHE_SLOTS = 16 };
/* One cache per native device. Caller serializes lookup/insertion; successful
 * lookup owns a shader reference. No native calls or allocator callbacks under
 * that lock: insertion returns the displaced owner for release after unlocking. */
typedef struct native_shader_cache {
    doe_shader *entries[NATIVE_SHADER_CACHE_SLOTS];
    size_t next;
    uint8_t image_identity[32];
    bool image_identity_ready;
} native_shader_cache;
doe_shader *native_shader_cache_module(native_shader_cache *, const doe_shader_options *, const char *, size_t);
doe_shader *native_shader_cache_stage(native_shader_cache *, const doe_shader *,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *);
doe_shader *native_shader_cache_insert(native_shader_cache *, doe_shader *);
bool native_shader_cache_admits(const doe_shader *);
void native_shader_cache_clear(native_shader_cache *);
#endif
