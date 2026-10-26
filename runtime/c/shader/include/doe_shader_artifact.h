/* SPDX-License-Identifier: MIT */
#ifndef DOE_SHADER_ARTIFACT_H
#define DOE_SHADER_ARTIFACT_H
#include "doe_shader.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Local-build immutable target serialization. This is not a portable ABI or a
 * signature/authentication format. The file owner supplies an independently
 * verified compiler/build identity; cache data is never a trust boundary.
 * Import verifies integrity, exact specialization identity and all structural
 * spans before publication; invalid/corrupt data leaves *out unchanged.
 * The shader owner has no filesystem, environment, device or cache policy. */
bool doe_shader_artifact_key(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *, uint8_t key[32]);
size_t doe_shader_artifact_size(const doe_shader *shader);
bool doe_shader_artifact_write(const doe_shader *, void *bytes, size_t size);
doe_shader_status doe_shader_artifact_read(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *, const void *bytes, size_t size, doe_shader **out);
#ifdef __cplusplus
}
#endif
#endif
