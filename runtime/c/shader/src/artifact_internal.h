/* SPDX-License-Identifier: MIT */
#ifndef DOE_SHADER_ARTIFACT_INTERNAL_H
#define DOE_SHADER_ARTIFACT_INTERNAL_H
#include "internal.h"
#include "doe_shader_artifact.h"
#include "../../common/include/doe_sha256.h"
/* Every persisted row type is a pointer-free compiler value. Never serialize
 * doe_shader, doe_shader_view, allocator hooks or process addresses. */
#define SHADER_ARTIFACT_SPANS(X) \
 X(words,uint32_t,counts.words) \
 X(types,DoeCompilerTypeV4,counts.types) \
 X(members,DoeCompilerMemberV4,counts.members) \
 X(resources,DoeCompilerResourceV4,counts.resources) \
 X(workgroups,DoeCompilerWorkgroupV5,counts.workgroups) \
 X(entries,DoeCompilerEntryV5,counts.entries) \
 X(entry_references,uint32_t,counts.entry_references) \
 X(names,char,counts.name_bytes) \
 X(handles,DoeCompilerHandleV6,handle_count) \
 X(interfaces,DoeCompilerStageInterfaceV7,interface_count) \
 X(text,char,text_size) \
 X(metal_bindings,DoeCompilerMetalBindingV12,metal_binding_count) \
 X(metal_entries,DoeCompilerMetalEntryV12,metal_entry_count) \
 X(hlsl_bindings,DoeCompilerHlslBindingV11,hlsl_binding_count) \
 X(hlsl_entries,DoeCompilerHlslEntryV11,hlsl_entry_count) \
 X(sampling_pairs,DoeCompilerSamplingPairV16,sampling_pair_count) \
 X(translation_entries,DoeCompilerTranslationEntryV17,translation_entry_count)
enum { SHADER_ARTIFACT_SPAN_COUNT = 17, SHADER_ARTIFACT_MAX_BYTES = 1024 * 1024 };
typedef struct shader_artifact_header {
    uint8_t digest[32];
    uint8_t key[32];
    uint32_t magic, version, byte_order, target, language, sample_mask;
    DoeCompilerModuleV5 module;
    uint64_t counts[SHADER_ARTIFACT_SPAN_COUNT];
} shader_artifact_header;
typedef struct shader_hlsl_artifact_header {
    uint8_t digest[32];
    uint32_t magic, version;
    uint64_t artifact_bytes;
    DoeCompilerHlslProfileV19 profile;
} shader_hlsl_artifact_header;
bool shader_artifact_valid(const doe_shader_view *);
typedef struct shader_artifact_walk { size_t type, next; } shader_artifact_walk;
bool shader_artifact_acyclic(const doe_shader_view *, uint8_t *, shader_artifact_walk *);
#endif
