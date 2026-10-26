/* SPDX-License-Identifier: MIT */
#include "artifact_internal.h"

static void key_add(uint8_t key[32], const void *bytes, size_t size)
{
    uint8_t row[72] = {0};
    memcpy(row,key,32);
    for (unsigned i = 0; i < 8; ++i) row[32+i] = (uint8_t)((uint64_t)size >> (8*i));
    doe_common_sha256(bytes,size,row+40);
    doe_common_sha256(row,sizeof(row),key);
}

bool doe_shader_artifact_key(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline, uint8_t key[32])
{
    if (!key || !module || module->kind != SHADER_MODULE || !selection || selection->reserved ||
        (selection->mode != DOE_COMPILER_SELECT_AUTO && selection->mode != DOE_COMPILER_SELECT_NAME) ||
        (!selection->name && selection->name_length) || selection->name_length > module->source_size ||
        (selection->mode == DOE_COMPILER_SELECT_AUTO && (selection->name || selection->name_length)) ||
        (count && !values) || count > module->override_count ||
        (pipeline && module->options.target != DOE_SHADER_METAL)) return false;
    static const char domain[] = "doe.c.shader.artifact.specialization.v1";
    doe_common_sha256(domain,sizeof(domain)-1,key);
    const uint32_t policy[] = {(uint32_t)module->options.target,module->semantic_flags,
        selection->mode,pipeline ? pipeline->sample_mask : UINT32_MAX};
    key_add(key,policy,sizeof(policy));
    key_add(key,module->source,module->source_size);
    key_add(key,module->identity,module->identity_size);
    key_add(key,selection->name,selection->name_length);
    for (size_t i = 0; i < count; ++i) {
        if ((!values[i].key && values[i].key_length) || values[i].key_length > module->source_size) return false;
        key_add(key,values[i].key,values[i].key_length);
        key_add(key,&values[i].value,sizeof(values[i].value));
    }
    return true;
}

static bool artifact_layout(const shader_artifact_header *header, size_t offsets[SHADER_ARTIFACT_SPAN_COUNT], size_t *size)
{
    size_t i = 0;
    *size = sizeof(*header);
#define RESERVE(field,type,count) \
    do { if (header->counts[i] > SIZE_MAX || !shader_reserve(size,(size_t)header->counts[i],sizeof(type),alignof(type),&offsets[i])) return false; ++i; } while (0);
    SHADER_ARTIFACT_SPANS(RESERVE)
#undef RESERVE
    return *size <= SHADER_ARTIFACT_MAX_BYTES;
}

static bool header_for(const doe_shader *shader, shader_artifact_header *header)
{
    if (!shader || shader->kind != SHADER_SPECIALIZED || !shader->view.translation_complete ||
        !shader->view.sampling_pairs_complete) return false;
    *header = (shader_artifact_header){.magic=0x53434f44u,.version=1,.byte_order=0x01020304u,
        .target=(uint32_t)shader->view.target,.language=shader->view.target_language_version,
        .sample_mask=shader->view.metal_sample_mask,.module=shader->view.module};
    const DoeCompilerEntrySelectionV5 selection = {.mode=shader->selection_mode,
        .name=shader->selection_mode == DOE_COMPILER_SELECT_AUTO ? NULL : shader->selection_name,
        .name_length=shader->selection_size};
    const DoeCompilerMetalPipelineOptionsV15 pipeline = {.sample_mask=shader->view.metal_sample_mask};
    if (!doe_shader_artifact_key(shader->module,&selection,shader->values,shader->value_count,
            shader->view.target == DOE_SHADER_METAL ? &pipeline : NULL,header->key)) return false;
    size_t i = 0;
#define COUNT(field,type,count) header->counts[i++] = shader->view.count;
    SHADER_ARTIFACT_SPANS(COUNT)
#undef COUNT
    return true;
}

size_t doe_shader_artifact_size(const doe_shader *shader)
{
    shader_artifact_header header;
    size_t offsets[SHADER_ARTIFACT_SPAN_COUNT], size;
    return header_for(shader,&header) && artifact_layout(&header,offsets,&size) ? size : 0;
}

bool doe_shader_artifact_write(const doe_shader *shader, void *bytes, size_t size)
{
    shader_artifact_header header;
    size_t offsets[SHADER_ARTIFACT_SPAN_COUNT], required;
    if (!bytes || !header_for(shader,&header) || !artifact_layout(&header,offsets,&required) || size != required) return false;
    memset(bytes,0,size);
    memcpy(bytes,&header,sizeof(header));
    size_t i = 0;
#define WRITE(field,type,count) \
    do { if (header.counts[i]) memcpy((uint8_t *)bytes+offsets[i],shader->view.field,(size_t)header.counts[i]*sizeof(type)); ++i; } while (0);
    SHADER_ARTIFACT_SPANS(WRITE)
#undef WRITE
    doe_common_sha256((uint8_t *)bytes+32,size-32,header.digest);
    memcpy(bytes,header.digest,32);
    return true;
}

doe_shader_status doe_shader_artifact_read(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection, const DoeCompilerOverrideValueV13 *values, size_t count,
    const DoeCompilerMetalPipelineOptionsV15 *pipeline, const void *bytes, size_t size, doe_shader **out)
{
    uint8_t key[32], digest[32];
    shader_artifact_header header;
    size_t offsets[SHADER_ARTIFACT_SPAN_COUNT], required;
    if (!out || !bytes || size < sizeof(header) || size > SHADER_ARTIFACT_MAX_BYTES ||
        !doe_shader_artifact_key(module,selection,values,count,pipeline,key)) return DOE_SHADER_INVALID_ARGUMENT;
    memcpy(&header,bytes,sizeof(header));
    doe_common_sha256((const uint8_t *)bytes+32,size-32,digest);
    if (memcmp(header.digest,digest,32) || memcmp(header.key,key,32) || header.magic != 0x53434f44u ||
        header.version != 1 || header.byte_order != 0x01020304u || header.target != (uint32_t)module->options.target ||
        header.sample_mask != (pipeline ? pipeline->sample_mask : UINT32_MAX) ||
        !artifact_layout(&header,offsets,&required) || required != size) return DOE_SHADER_INVALID_ARGUMENT;
    size_t total = sizeof(doe_shader), data_offset, values_offset, selection_offset, keys_offset, key_bytes = 0;
    size_t colors_offset, walk_offset;
    for (size_t i = 0; i < count; ++i) {
        if (values[i].key_length > SIZE_MAX-key_bytes) return DOE_SHADER_OVERFLOW;
        key_bytes += values[i].key_length;
    }
    if (!shader_reserve(&total,size,1,alignof(max_align_t),&data_offset) ||
        !shader_reserve(&total,count,sizeof(*values),alignof(DoeCompilerOverrideValueV13),&values_offset) ||
        !shader_reserve(&total,selection->name_length+1,1,1,&selection_offset) ||
        !shader_reserve(&total,key_bytes,1,1,&keys_offset) ||
        !shader_reserve(&total,(size_t)header.counts[1],1,1,&colors_offset) ||
        !shader_reserve(&total,(size_t)header.counts[1],sizeof(shader_artifact_walk),alignof(shader_artifact_walk),&walk_offset))
        return DOE_SHADER_OVERFLOW;
    if (total > 2u * SHADER_ARTIFACT_MAX_BYTES) return DOE_SHADER_INVALID_ARGUMENT;
    doe_shader_status retained = doe_shader_retain((doe_shader *)module);
    if (retained != DOE_SHADER_OK) return retained;
    doe_shader *shader = module->allocator.allocate(module->allocator.userdata,total,alignof(max_align_t));
    if (!shader) { doe_shader_release((doe_shader *)module); return DOE_SHADER_OUT_OF_MEMORY; }
    /* Recheck caller identity after allocator reentry, before using any extents. */
    if (!doe_shader_artifact_key(module,selection,values,count,pipeline,key) || memcmp(header.key,key,32)) {
        module->allocator.deallocate(module->allocator.userdata,shader,total,alignof(max_align_t));
        doe_shader_release((doe_shader *)module);
        return DOE_SHADER_INVALID_ARGUMENT;
    }
    memset(shader,0,total);
    atomic_init(&shader->references,1);
    shader->kind = SHADER_SPECIALIZED;
    shader->module = (doe_shader *)module;
    shader->allocator = module->allocator;
    shader->allocation_size = total;
    shader->source = module->source; shader->source_size = module->source_size;
    shader->identity = module->identity; shader->identity_size = module->identity_size;
    shader->semantic_flags = module->semantic_flags;
    shader->selection_mode = selection->mode; shader->selection_size = selection->name_length;
    shader->selection_name = (char *)shader+selection_offset;
    if (selection->name_length) memcpy(shader->selection_name,selection->name,selection->name_length);
    shader->values = count ? (DoeCompilerOverrideValueV13 *)((uint8_t *)shader+values_offset) : NULL;
    shader->value_count = count;
    char *keys = (char *)shader+keys_offset;
    for (size_t i = 0; i < count; ++i) {
        shader->values[i] = values[i]; shader->values[i].key = keys;
        if (values[i].key_length) memcpy(keys,values[i].key,values[i].key_length);
        keys += values[i].key_length;
    }
    uint8_t *data = (uint8_t *)shader+data_offset;
    memcpy(data,bytes,size);
    shader->view = (doe_shader_view){.target=module->options.target,.target_language_version=header.language,
        .metal_sample_mask=header.sample_mask,.module=header.module,.sampling_pairs_complete=true,.translation_complete=true};
    size_t i = 0;
#define READ(field,type,count_field) \
    shader->view.count_field = (size_t)header.counts[i]; \
    shader->view.field = header.counts[i] ? (const type *)(data+offsets[i]) : NULL; ++i;
    SHADER_ARTIFACT_SPANS(READ)
#undef READ
    /* Verify copied bytes too: an allocator callback may mutate the caller blob. */
    doe_common_sha256(data+32,size-32,digest);
    if (memcmp(header.digest,digest,32) || !shader_artifact_valid(&shader->view) ||
        !shader_artifact_acyclic(&shader->view,(uint8_t *)shader+colors_offset,
            (shader_artifact_walk *)((uint8_t *)shader+walk_offset))) {
        doe_shader_release(shader); return DOE_SHADER_INVALID_ARGUMENT;
    }
    *out = shader;
    return DOE_SHADER_OK;
}
