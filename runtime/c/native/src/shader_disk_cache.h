/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_SHADER_DISK_CACHE_H
#define DOE_NATIVE_SHADER_DISK_CACHE_H
#include "doe_shader_artifact.h"
/* These are optional acceleration operations: misses/corruption/I/O/OOM never
 * replace the compiler result. Caller holds ordinary immutable module leases. */
doe_shader *native_shader_disk_lookup(const uint8_t image_identity[32], const doe_shader *,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *);
void native_shader_disk_store(const uint8_t image_identity[32], const doe_shader *,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *, const doe_shader *);
#endif
