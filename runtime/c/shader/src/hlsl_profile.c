/* SPDX-License-Identifier: MIT */
#include "artifact_internal.h"

static bool consistent(const doe_shader_view *view, const DoeCompilerHlslProfileV19 *profile)
{
    if (view->target != DOE_SHADER_HLSL || view->target_language_version != 600u ||
        view->counts.entries != 1u || view->hlsl_entry_count != 1u || !view->entries || !view->hlsl_entries ||
        profile->entry_index != 0u || profile->reserved || profile->stage != view->entries[0].stage ||
        view->hlsl_entries[0].entry_index != 0u || view->hlsl_entries[0].stage != profile->stage ||
        (profile->shader_model != 501u && profile->shader_model != 600u && profile->shader_model != 620u)) return false;
    const uint32_t flags = view->hlsl_entries[0].flags;
    if ((profile->shader_model == 620u) != ((flags & DOE_COMPILER_HLSL_ENTRY_NATIVE16) != 0u)) return false;
    /* A supplied positive certificate must not contradict native requirements.
     * Zero flags alone never produces or restores a certificate. STORAGE_SIZES
     * is a binding (size-table CBV) requirement, not a shader-model one. */
    if (profile->shader_model == 501u && (profile->stage != DOE_COMPILER_STAGE_COMPUTE ||
        (flags & ~DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES) != 0u)) return false;
    return true;
}

bool doe_shader_hlsl_profile(const doe_shader *shader, DoeCompilerHlslProfileV19 *out)
{
    if (!shader || !out || !shader->hlsl_profile_complete ||
        !consistent(&shader->view,&shader->hlsl_profile)) return false;
    *out = shader->hlsl_profile;
    return true;
}

size_t doe_shader_hlsl_artifact_size(const doe_shader *shader)
{
    DoeCompilerHlslProfileV19 profile;
    if (!doe_shader_hlsl_profile(shader,&profile)) return 0;
    const size_t legacy = doe_shader_artifact_size(shader);
    return legacy && legacy <= SIZE_MAX-sizeof(shader_hlsl_artifact_header) ?
        sizeof(shader_hlsl_artifact_header)+legacy : 0;
}

bool doe_shader_hlsl_artifact_write(const doe_shader *shader, void *bytes, size_t size)
{
    DoeCompilerHlslProfileV19 profile;
    const size_t expected = doe_shader_hlsl_artifact_size(shader);
    if (!bytes || !expected || size != expected || !doe_shader_hlsl_profile(shader,&profile)) return false;
    shader_hlsl_artifact_header header = {.magic=0x50484f44u,.version=1u,
        .artifact_bytes=size-sizeof(header),.profile=profile};
    if (!doe_shader_artifact_write(shader,(uint8_t *)bytes+sizeof(header),size-sizeof(header))) return false;
    memcpy(bytes,&header,sizeof(header));
    doe_common_sha256((uint8_t *)bytes+32u,size-32u,header.digest);
    memcpy(bytes,header.digest,sizeof(header.digest));
    return true;
}

doe_shader_status doe_shader_hlsl_artifact_read(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline, const void *bytes, size_t size, doe_shader **out)
{
    if (!module || !out || !bytes || module->options.target != DOE_SHADER_HLSL ||
        size < sizeof(shader_hlsl_artifact_header) ||
        size-sizeof(shader_hlsl_artifact_header) > SHADER_ARTIFACT_MAX_BYTES) return DOE_SHADER_INVALID_ARGUMENT;
    /* Snapshot the pointer-free profile before an allocator callback. The
     * legacy reader independently verifies/copies its original artifact digest
     * after reentry, so the restored association is with those exact bytes. */
    shader_hlsl_artifact_header header;
    uint8_t digest[32];
    memcpy(&header,bytes,sizeof(header));
    doe_common_sha256((const uint8_t *)bytes+32u,size-32u,digest);
    if (memcmp(header.digest,digest,sizeof(digest)) || header.magic != 0x50484f44u ||
        header.version != 1u || header.artifact_bytes != size-sizeof(header) || header.profile.reserved ||
        header.profile.entry_index != 0u ||
        (header.profile.shader_model != 501u && header.profile.shader_model != 600u &&
            header.profile.shader_model != 620u)) return DOE_SHADER_INVALID_ARGUMENT;
    doe_shader *shader = NULL;
    const doe_shader_status status = doe_shader_artifact_read(module,selection,values,count,pipeline,
        (const uint8_t *)bytes+sizeof(header),size-sizeof(header),&shader);
    if (status != DOE_SHADER_OK) return status;
    if (!consistent(&shader->view,&header.profile)) {
        doe_shader_release(shader);
        return DOE_SHADER_INVALID_ARGUMENT;
    }
    shader->hlsl_profile = header.profile;
    shader->hlsl_profile_complete = true;
    *out = shader;
    return DOE_SHADER_OK;
}
