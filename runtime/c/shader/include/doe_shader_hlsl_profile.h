/* SPDX-License-Identifier: MIT */
#ifndef DOE_SHADER_HLSL_PROFILE_H
#define DOE_SHADER_HLSL_PROFILE_H
#include "doe_shader_artifact.h"
#include "doe_compiler_hlsl_profile.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional immutable compiler-produced requirement. Legacy creation/artifact
 * import has no certificate; false leaves *out unchanged. Retain shader while
 * borrowing. No inference from target text or legacy ABI11 flags occurs. */
bool doe_shader_hlsl_profile(const doe_shader *, DoeCompilerHlslProfileV19 *out);
/* Additive local-build envelope. Legacy artifact bytes/APIs are unchanged.
 * The envelope binds the profile and exact legacy artifact under one digest;
 * it is integrity/structural validation, not authentication or a trust boundary.
 * The independently verified compiler/build identity remains caller-owned. */
size_t doe_shader_hlsl_artifact_size(const doe_shader *);
bool doe_shader_hlsl_artifact_write(const doe_shader *, void *bytes, size_t size);
doe_shader_status doe_shader_hlsl_artifact_read(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *, const DoeCompilerOverrideValueV13 *, size_t,
    const DoeCompilerMetalPipelineOptionsV15 *, const void *bytes, size_t size, doe_shader **out);
#ifdef __cplusplus
}
#endif
#endif
