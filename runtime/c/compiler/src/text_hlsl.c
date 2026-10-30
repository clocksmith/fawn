/* SPDX-License-Identifier: MIT
 * Publish metadata measured by the actual HLSL declaration/entry owners. */
#include "text_internal.h"

bool compiler_text_hlsl(Source *source, uint32_t selected, DoeTargetScratch scratch,
    DoeCompilerHlslBindingV11 *bindings, DoeCompilerHlslEntryV11 *entries, TextTargetResult *result) {
    const CompilerWorkspace *c = source->compiler;
    *result = (TextTargetResult){0};
    if (c->entry_count == 0u) return true;
    SamplingContext *sampling = sampling_context(source);
    const bool profile = sampling && sampling->translation && sampling->translation->hlsl_profile;
    DoeHlslResult measured;
    if (doe_hlsl_emit_metadata(source, c, selected, scratch,
            (DoeHlslMetadata){.bindings=bindings,.entries=entries,.profile_requirements=profile},
            &measured) != DOE_COMPILER_OK) return false;
    result->required_bytes = measured.required_bytes;
    if (profile) {
        const uint32_t index = selected == NO_ID ? 0u : selected;
        const DoeCompilerHlslEntryV11 *entry = &entries[index];
        *sampling->translation->hlsl_profile = (DoeCompilerHlslProfileV19){
            .entry_index=entry->entry_index,.stage=entry->stage,.shader_model=measured.shader_model};
    }
    /* Input pools already bound both loops. Compact declaration-indexed staging
     * in source order; existing entry_references own each entry's membership. */
    for (uint32_t i = 0u; i < c->resource_count; ++i)
        if (bindings[i].register_kind != 0u) bindings[result->bindings++] = bindings[i];
    for (uint32_t i = 0u; i < c->entry_count; ++i)
        if (selected == NO_ID || selected == i) entries[result->entries++] = entries[i];
    return true;
}
