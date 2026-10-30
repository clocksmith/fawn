/* SPDX-License-Identifier: MIT
 * Host ownership/integrity controls; no FXC, SDK or GPU execution. */
#include "doe_shader_hlsl_profile.h"
#include "../src/artifact_internal.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { MUTATE_NONE, MUTATE_HEADER, MUTATE_BODY, MUTATE_SELECTION } mutation;
typedef struct {
    unsigned calls, fail_at, live, mutate_at;
    mutation action;
    uint8_t *bytes;
    size_t size;
    char *selection;
    struct { void *memory; size_t size, alignment; } blocks[32];
} allocations;

static void *allocate(void *userdata, size_t size, size_t alignment)
{
    allocations *a = userdata;
    assert(size && alignment <= alignof(max_align_t));
    ++a->calls;
    if (a->calls == a->mutate_at) {
        if (a->action == MUTATE_HEADER) {
            shader_hlsl_artifact_header header;
            memcpy(&header,a->bytes,sizeof(header));
            header.profile.shader_model = 620;
            header.profile.stage = DOE_COMPILER_STAGE_VERTEX;
            header.profile.reserved = 1;
            memcpy(a->bytes,&header,sizeof(header));
        } else if (a->action == MUTATE_BODY) a->bytes[a->size-1] ^= 1;
        else if (a->action == MUTATE_SELECTION) memcpy(a->selection,"other",5);
    }
    if (a->calls == a->fail_at) return NULL;
    void *memory = malloc(size);
    if (memory) {
        size_t slot = 0;
        while (slot < 32 && a->blocks[slot].memory) ++slot;
        assert(slot < 32);
        a->blocks[slot].memory = memory;
        a->blocks[slot].size = size;
        a->blocks[slot].alignment = alignment;
        ++a->live;
    }
    return memory;
}

static void deallocate(void *userdata, void *memory, size_t size, size_t alignment)
{
    allocations *a = userdata;
    size_t slot = 0;
    while (slot < 32 && a->blocks[slot].memory != memory) ++slot;
    assert(slot < 32 && a->live && a->blocks[slot].size == size && a->blocks[slot].alignment == alignment);
    a->blocks[slot].memory = NULL;
    --a->live;
    free(memory);
}

static doe_shader_options options(allocations *a)
{
    return (doe_shader_options){.target=DOE_SHADER_HLSL,.allocator={a,allocate,deallocate},
        .capacities={.typed={.source_bytes=8192,.tokens=1024,.statements=256,.expressions=1024,
            .types=64,.members=128,.resources=16,.access_steps=1024,.constants=256,.constant_edges=512,
            .ir_instructions=4096,.ir_operands=2048,.target_declarations=4096,.traversal_items=193,
            .words=32768,.expression_depth=32,.control_depth=16,.type_syntax_depth=32},
            .entries=8,.entry_inputs=64,.entry_references=128},
        .interface_attributes=64,.stage_interfaces=64,.sampling_pairs=128,
        .compiler_identity="hlsl-profile-owner-v19",.compiler_identity_size=sizeof("hlsl-profile-owner-v19")-1,
        .semantic_flags=DOE_COMPILER_CORE_BINDING_NUMBERS};
}

static DoeCompilerEntrySelectionV5 selection(const char *name)
{
    return (DoeCompilerEntrySelectionV5){.mode=DOE_COMPILER_SELECT_NAME,.name=name,.name_length=strlen(name)};
}

static void profile_is(const doe_shader *shader, uint32_t model)
{
    DoeCompilerHlslProfileV19 profile;
    memset(&profile,0xa5,sizeof(profile));
    assert(doe_shader_hlsl_profile(shader,&profile));
    assert(profile.entry_index == 0 && profile.stage == DOE_COMPILER_STAGE_COMPUTE);
    assert(profile.shader_model == model && !profile.reserved);
    doe_shader_view view;
    assert(doe_shader_borrow(shader,&view) == DOE_SHADER_OK && view.target_language_version == 600);
    assert(view.hlsl_entry_count == 1);
    if (model == 501) assert(!view.hlsl_entries[0].flags); /* Legacy ABI11 row stays unchanged. */
}

static void uncertified(const doe_shader *shader)
{
    DoeCompilerHlslProfileV19 profile, original;
    memset(&profile,0xa5,sizeof(profile));
    original = profile;
    assert(!doe_shader_hlsl_profile(shader,&profile));
    assert(!memcmp(&profile,&original,sizeof(profile)));
    assert(!doe_shader_hlsl_artifact_size(shader));
}

static void rehash(uint8_t *bytes, size_t size)
{
    uint8_t digest[32];
    assert(size >= sizeof(shader_hlsl_artifact_header));
    doe_common_sha256(bytes+32,size-32,digest);
    memcpy(bytes,digest,sizeof(digest));
}

static void rejected(allocations *a, const doe_shader *module, const DoeCompilerEntrySelectionV5 *entry,
    const uint8_t *bytes, size_t size)
{
    doe_shader *sentinel = (doe_shader *)(uintptr_t)1;
    doe_shader *out = sentinel;
    unsigned live = a->live;
    assert(doe_shader_hlsl_artifact_read(module,entry,NULL,0,NULL,bytes,size,&out) != DOE_SHADER_OK);
    assert(out == sentinel && a->live == live);
}

static uint8_t *envelope(const doe_shader *shader, size_t *size)
{
    *size = doe_shader_hlsl_artifact_size(shader);
    assert(*size > sizeof(shader_hlsl_artifact_header));
    uint8_t *bytes = malloc(*size);
    assert(bytes && doe_shader_hlsl_artifact_write(shader,bytes,*size));
    return bytes;
}

static void roundtrip_models(void)
{
    static const char *const sources[] = {
        "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@compute @workgroup_size(1) fn first(){a[0]=min(a[0],7u);}",
        "enable f16;@group(0) @binding(0) var<storage,read_write> a:array<f32>;"
        "@compute @workgroup_size(1) fn first(){let x=f16(a[0]);a[0]=f32(x);}"
    };
    const uint32_t models[] = {600,620};
    for (size_t i = 0; i < 2; ++i) {
        allocations a = {0};
        doe_shader_options opts = options(&a);
        doe_shader *module = NULL, *shader = NULL, *restored = NULL;
        doe_shader_error error;
        DoeCompilerEntrySelectionV5 entry = selection("first");
        assert(doe_shader_create_module(&opts,0,sources[i],strlen(sources[i]),&module,&error) == DOE_SHADER_OK);
        assert(doe_shader_specialize(module,&entry,NULL,0,&shader,&error) == DOE_SHADER_OK);
        profile_is(shader,models[i]);
        size_t size;
        uint8_t *bytes = envelope(shader,&size);
        assert(doe_shader_hlsl_artifact_read(module,&entry,NULL,0,NULL,bytes,size,&restored) == DOE_SHADER_OK);
        profile_is(restored,models[i]);
        if (models[i] == 620) {
            shader_hlsl_artifact_header header;
            memcpy(&header,bytes,sizeof(header));
            header.profile.shader_model = 501;
            memcpy(bytes,&header,sizeof(header));
            rehash(bytes,size);
            rejected(&a,module,&entry,bytes,size); /* Rehashed native16 contradiction. */
        }
        free(bytes);
        doe_shader_release(restored); doe_shader_release(shader); doe_shader_release(module);
        assert(!a.live);
    }
}

int main(void)
{
    static const char source[] =
        "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@compute @workgroup_size(4) fn first(){a[0]=1u;}"
        "@compute @workgroup_size(4) fn other(){a[0]=2u;}";
    allocations a = {0};
    doe_shader_options opts = options(&a);
    doe_shader *module = NULL, *shader = NULL, *other = NULL;
    doe_shader_error error;
    char name[] = "first";
    DoeCompilerEntrySelectionV5 entry = selection(name), other_entry = selection("other");
    assert(doe_shader_create_module(&opts,0,source,sizeof(source)-1,&module,&error) == DOE_SHADER_OK);
    uncertified(module); uncertified(NULL);
    unsigned before = a.calls;
    assert(doe_shader_specialize(module,&entry,NULL,0,&shader,&error) == DOE_SHADER_OK);
    unsigned acquisitions = a.calls-before;
    assert(acquisitions);
    profile_is(shader,501);
    assert(doe_shader_specialization_matches(shader,module,&entry,NULL,0));
    assert(doe_shader_retain(shader) == DOE_SHADER_OK);
    const doe_shader *same = shader;
    doe_shader_release(shader);
    assert(same == shader);
    profile_is(same,501); /* Retained object used by the native memory cache. */
    assert(doe_shader_specialize(module,&other_entry,NULL,0,&other,&error) == DOE_SHADER_OK);
    profile_is(other,501);

    size_t size, other_size;
    uint8_t *bytes = envelope(shader,&size), *other_bytes = envelope(other,&other_size);
    const size_t legacy_size = doe_shader_artifact_size(shader);
    uint8_t *legacy = malloc(legacy_size), *again = malloc(legacy_size), *copy = malloc(size+1);
    assert(legacy && again && copy && size == sizeof(shader_hlsl_artifact_header)+legacy_size);
    assert(doe_shader_artifact_write(shader,legacy,legacy_size));
    assert(!memcmp(bytes+sizeof(shader_hlsl_artifact_header),legacy,legacy_size));
    doe_shader *restored = NULL, *old = NULL;
    assert(doe_shader_hlsl_artifact_read(module,&entry,NULL,0,NULL,bytes,size,&restored) == DOE_SHADER_OK);
    profile_is(restored,501);
    assert(doe_shader_artifact_write(restored,again,legacy_size) && !memcmp(again,legacy,legacy_size));
    assert(doe_shader_artifact_read(module,&entry,NULL,0,NULL,legacy,legacy_size,&old) == DOE_SHADER_OK);
    uncertified(old);
    assert(doe_shader_artifact_write(old,again,legacy_size) && !memcmp(again,legacy,legacy_size));
    memset(copy,0xcc,size+1);
    assert(!doe_shader_hlsl_artifact_write(old,copy,size));
    for (size_t i = 0; i <= size; ++i) assert(copy[i] == 0xcc);

    const size_t truncations[] = {0,1,sizeof(shader_hlsl_artifact_header)-1,
        sizeof(shader_hlsl_artifact_header),size-1};
    for (size_t i = 0; i < sizeof(truncations)/sizeof(*truncations); ++i)
        rejected(&a,module,&entry,bytes,truncations[i]);
    memcpy(copy,bytes,size); copy[size] = 0;
    rejected(&a,module,&entry,copy,size+1);
    for (unsigned variant = 0; variant < 10; ++variant) {
        memcpy(copy,bytes,size);
        shader_hlsl_artifact_header header;
        memcpy(&header,copy,sizeof(header));
        switch (variant) {
        case 0: header.digest[0] ^= 1; break;
        case 1: header.magic ^= 1; break;
        case 2: ++header.version; break;
        case 3: ++header.artifact_bytes; break;
        case 4: header.profile.entry_index = 1; break;
        case 5: header.profile.stage = DOE_COMPILER_STAGE_VERTEX; break;
        case 6: header.profile.shader_model = 999; break;
        case 7: header.profile.shader_model = 620; break;
        case 8: header.profile.reserved = 1; break;
        case 9: copy[size-1] ^= 1; break;
        }
        memcpy(copy,&header,sizeof(header));
        if (variant) rehash(copy,size);
        rejected(&a,module,&entry,copy,size);
    }
    /* A valid other selected-stage body cannot be substituted under the first key. */
    assert(other_size == size);
    memcpy(copy,bytes,sizeof(shader_hlsl_artifact_header));
    memcpy(copy+sizeof(shader_hlsl_artifact_header),other_bytes+sizeof(shader_hlsl_artifact_header),legacy_size);
    rehash(copy,size);
    rejected(&a,module,&entry,copy,size);
    rejected(&a,module,&other_entry,bytes,size);

    /* A rehashed conservative600 record is structurally valid: this is not authentication. */
    memcpy(copy,bytes,size);
    shader_hlsl_artifact_header header;
    memcpy(&header,copy,sizeof(header)); header.profile.shader_model = 600;
    memcpy(copy,&header,sizeof(header)); rehash(copy,size);
    doe_shader *conservative = NULL;
    assert(doe_shader_hlsl_artifact_read(module,&entry,NULL,0,NULL,copy,size,&conservative) == DOE_SHADER_OK);
    profile_is(conservative,600);
    doe_shader_release(conservative);

    unsigned live = a.live;
    for (unsigned ordinal = 1; ordinal <= acquisitions; ++ordinal) {
        doe_shader *out = (doe_shader *)(uintptr_t)1;
        a.fail_at = a.calls+ordinal;
        assert(doe_shader_specialize(module,&entry,NULL,0,&out,&error) == DOE_SHADER_OUT_OF_MEMORY);
        assert(out == (doe_shader *)(uintptr_t)1 && a.live == live);
        a.fail_at = 0;
        profile_is(shader,501);
    }
    doe_shader *out = (doe_shader *)(uintptr_t)1;
    a.fail_at = a.calls+1;
    assert(doe_shader_hlsl_artifact_read(module,&entry,NULL,0,NULL,bytes,size,&out) == DOE_SHADER_OUT_OF_MEMORY);
    assert(out == (doe_shader *)(uintptr_t)1 && a.live == live);
    a.fail_at = 0;
    for (mutation action = MUTATE_HEADER; action <= MUTATE_SELECTION; ++action) {
        memcpy(copy,bytes,size); memcpy(name,"first",5);
        a.action = action; a.bytes = copy; a.size = size; a.selection = name; a.mutate_at = a.calls+1;
        out = (doe_shader *)(uintptr_t)1;
        doe_shader_status status = doe_shader_hlsl_artifact_read(module,&entry,NULL,0,NULL,copy,size,&out);
        a.mutate_at = 0; a.action = MUTATE_NONE;
        if (action == MUTATE_HEADER) {
            assert(status == DOE_SHADER_OK);
            profile_is(out,501); /* Header snapshot survives allocator reentry. */
            doe_shader_release(out);
        } else assert(status != DOE_SHADER_OK && out == (doe_shader *)(uintptr_t)1);
        assert(a.live == live);
    }
    memcpy(name,"first",5);
    doe_shader_release(old); doe_shader_release(other); doe_shader_release(shader);
    doe_shader_release(module); /* Restored shader independently retains the immutable module. */
    profile_is(restored,501);
    doe_shader_view view;
    assert(doe_shader_borrow(restored,&view) == DOE_SHADER_OK && view.text_size && view.counts.entries == 1);
    doe_shader_release(restored);
    assert(!a.live);
    free(bytes); free(other_bytes); free(legacy); free(again); free(copy);
    roundtrip_models();
    puts("PASS HLSL profile ownership/envelope:501/600/620, retained identity, legacy stability, corruption/rehash/swap, allocator reentry/failure and lifetime (host only)");
    return 0;
}
